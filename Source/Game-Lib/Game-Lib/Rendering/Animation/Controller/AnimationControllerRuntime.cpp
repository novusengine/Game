#include "AnimationControllerRuntime.h"

#include <Base/CVarSystem/CVarSystem.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <queue>

namespace
{
    AutoCVar_Float CVAR_AnimationControllerTimeScale(CVarCategory::Client | CVarCategory::Rendering, "animationControllerTimeScale", "Animation V2 controller time scale", 1.0f, CVarFlags::EditFloatDrag | CVarFlags::DoNotSave);

    template <typename T>
    void Append(std::vector<u8>& bytes, const T& value)
    {
        const size_t offset = bytes.size();
        bytes.resize(offset + sizeof(T));
        std::memcpy(bytes.data() + offset, &value, sizeof(T));
    }

    u32 ValueSize(FileFormat::Animation::GraphValueType type)
    {
        switch (type)
        {
            case FileFormat::Animation::GraphValueType::Bool: return 1;
            case FileFormat::Animation::GraphValueType::Int: return 4;
            case FileFormat::Animation::GraphValueType::Float: return 4;
            case FileFormat::Animation::GraphValueType::Float3: return 12;
            default: return 0;
        }
    }

    bool Compare(f32 value, Animation::GraphComparison comparison, f32 threshold)
    {
        switch (comparison)
        {
            case Animation::GraphComparison::Equal: return value == threshold;
            case Animation::GraphComparison::NotEqual: return value != threshold;
            case Animation::GraphComparison::Less: return value < threshold;
            case Animation::GraphComparison::LessEqual: return value <= threshold;
            case Animation::GraphComparison::Greater: return value > threshold;
            case Animation::GraphComparison::GreaterEqual: return value >= threshold;
        }
        return false;
    }

    f32 Curve(Animation::BlendCurve curve, f32 value)
    {
        value = std::clamp(value, 0.0f, 1.0f);
        return curve == Animation::BlendCurve::SmoothStep ? value * value * (3.0f - 2.0f * value) : value;
    }
}

namespace Animation
{
    u64 HashAnimationName(std::string_view value)
    {
        u64 hash = 0xCBF29CE484222325ull;
        for (const char character : value)
        {
            hash ^= static_cast<u8>(character);
            hash *= 0x100000001B3ull;
        }
        return hash;
    }

    GraphCompileResult CompileAnimationGraph(const EditableAnimationGraph& source)
    {
        GraphCompileResult result;
        auto Fail = [&result](std::string field, u32 index, std::string message) { result.diagnostics.push_back({std::move(field), index, std::move(message)}); };
        if (source.version != EditableAnimationGraph::CURRENT_VERSION)
            Fail("version", std::numeric_limits<u32>::max(), "editable graph source version is unsupported");
        if (source.states.empty())
            Fail("states", std::numeric_limits<u32>::max(), "graph has no states");
        if (source.maximumSnapshotJoints == 0 || source.maximumSnapshotJoints > 4096)
            Fail("maximumSnapshotJoints", std::numeric_limits<u32>::max(), "snapshot joint capacity is outside the fixed runtime limit");
        if (source.commandCapacity > 64 || source.actionCapacity > 64)
            Fail("queueCapacity", std::numeric_limits<u32>::max(), "controller queue capacity exceeds 64");

        auto graph = std::make_shared<CompiledAnimationGraph>();
        if (source.maximumPoseDomains == 0 || source.maximumPoseDomains > 4)
            Fail("maximumPoseDomains", std::numeric_limits<u32>::max(), "pose domain capacity must be between one and four");
        graph->maximumPoseDomains = source.maximumPoseDomains;
        graph->retargetChainCorrection = source.retargetChainCorrection;
        graph->maximumSnapshotJoints = source.maximumSnapshotJoints;
        graph->commandCapacity = source.commandCapacity;
        graph->actionCapacity = source.actionCapacity;
        u32 parameterOffset = 0;
        for (u32 index = 0; index < source.parameters.size(); ++index)
        {
            const EditableGraphParameter& parameter = source.parameters[index];
            const u64 nameHash = HashAnimationName(parameter.name);
            const u32 size = ValueSize(parameter.type);
            if (parameter.name.empty() || nameHash == 0 || size == 0)
            {
                Fail("parameters", index, "parameter name or type is invalid");
                continue;
            }
            if (std::any_of(graph->parameters.begin(), graph->parameters.end(), [nameHash](const CompiledGraphParameter& value) { return value.nameHash == nameHash; }))
            {
                Fail("parameters", index, "duplicate parameter name");
                continue;
            }
            parameterOffset = (parameterOffset + 3u) & ~3u;
            graph->parameters.push_back({nameHash, parameterOffset, parameter.type, parameter.oneFrameTrigger});
            graph->defaultParameters.resize(parameterOffset + size);
            if (parameter.type == FileFormat::Animation::GraphValueType::Bool)
                graph->defaultParameters[parameterOffset] = parameter.defaultBool ? 1 : 0;
            else if (parameter.type == FileFormat::Animation::GraphValueType::Int)
                std::memcpy(graph->defaultParameters.data() + parameterOffset, &parameter.defaultInt, sizeof(i32));
            else if (parameter.type == FileFormat::Animation::GraphValueType::Float)
                std::memcpy(graph->defaultParameters.data() + parameterOffset, &parameter.defaultFloat, sizeof(f32));
            else if (parameter.type == FileFormat::Animation::GraphValueType::Float3)
                std::memcpy(graph->defaultParameters.data() + parameterOffset, &parameter.defaultFloat3, sizeof(vec3));
            parameterOffset += size;
        }
        graph->parameterBytes = parameterOffset;

        for (u32 index = 0; index < source.states.size(); ++index)
        {
            const EditableGraphState& state = source.states[index];
            if (state.sourceGUID == 0 || state.durationMicroseconds == 0 || state.kind > GraphStateKind::BlendSpace2D)
                Fail("states", index, "state GUID, kind, and duration must be valid");
            if (state.kind == GraphStateKind::Clip && state.clipSlotSemanticID == 0)
                Fail("states", index, "clip state semantic slot must be nonzero");
            if (std::any_of(graph->states.begin(), graph->states.end(), [&state](const CompiledGraphState& value) { return value.sourceGUID == state.sourceGUID; }))
                Fail("states", index, "duplicate state GUID");
            CompiledGraphState compiledState;
            compiledState.sourceGUID = state.sourceGUID;
            compiledState.clipSlotSemanticID = state.clipSlotSemanticID;
            compiledState.durationMicroseconds = state.durationMicroseconds;
            compiledState.kind = state.kind;
            compiledState.smoothingHalfLifeSeconds = state.smoothingHalfLifeSeconds;
            compiledState.wrapX = state.wrapX;
            compiledState.wrapMinimum = state.wrapMinimum;
            compiledState.wrapMaximum = state.wrapMaximum;
            auto ResolveFloatParameter = [&](const std::string& name, u16& output) {
                const u64 hash = HashAnimationName(name);
                const auto found = std::find_if(graph->parameters.begin(), graph->parameters.end(), [hash](const CompiledGraphParameter& value) { return value.nameHash == hash; });
                if (name.empty() || found == graph->parameters.end() || found->type != FileFormat::Animation::GraphValueType::Float)
                    return false;
                output = static_cast<u16>(std::distance(graph->parameters.begin(), found));
                return true;
            };
            if (state.kind != GraphStateKind::Clip)
            {
                if (!std::isfinite(state.smoothingHalfLifeSeconds) || state.smoothingHalfLifeSeconds < 0.0f || !ResolveFloatParameter(state.inputX, compiledState.inputXParameter))
                    Fail("states", index, "blend space X input or smoothing is invalid");
                if (state.kind == GraphStateKind::BlendSpace2D && !ResolveFloatParameter(state.inputY, compiledState.inputYParameter))
                    Fail("states", index, "2D blend space Y input is invalid");
                if (state.blendSamples.empty() || state.blendSamples.size() > 64 || (state.kind == GraphStateKind::BlendSpace2D && state.blendSamples.size() < 3))
                    Fail("states", index, "blend sample count is outside the compiled limit");
                compiledState.blendSampleOffset = static_cast<u16>(graph->blendSamples.size());
                compiledState.blendSampleCount = static_cast<u16>(state.blendSamples.size());
                for (u32 sampleIndex = 0; sampleIndex < state.blendSamples.size(); ++sampleIndex)
                {
                    const EditableGraphBlendSample& sample = state.blendSamples[sampleIndex];
                    if (sample.clipSlotSemanticID == 0 || !std::isfinite(sample.position.x) || !std::isfinite(sample.position.y) || (state.kind == GraphStateKind::BlendSpace1D && sampleIndex != 0 && !(sample.position.x > state.blendSamples[sampleIndex - 1].position.x)))
                        Fail("blendSamples", sampleIndex, "blend sample semantic or position is invalid/duplicate");
                    graph->blendSamples.push_back({sample.position, sample.clipSlotSemanticID});
                }
                compiledState.blendTriangleOffset = static_cast<u16>(graph->blendTriangles.size());
                compiledState.blendTriangleCount = static_cast<u16>(state.blendTriangles.size());
                if (state.kind == GraphStateKind::BlendSpace2D && state.blendTriangles.empty())
                    Fail("blendTriangles", index, "2D blend space requires offline triangles");
                for (const BlendSpaceTriangle& triangle : state.blendTriangles)
                {
                    if (triangle.samples[0] >= state.blendSamples.size() || triangle.samples[1] >= state.blendSamples.size() || triangle.samples[2] >= state.blendSamples.size())
                        Fail("blendTriangles", index, "triangle sample index is out of range");
                    graph->blendTriangles.push_back(triangle);
                }
            }
            graph->states.push_back(compiledState);
        }
        const auto entry = std::find_if(graph->states.begin(), graph->states.end(), [&source](const CompiledGraphState& value) { return value.sourceGUID == source.entryStateGUID; });
        if (entry == graph->states.end())
            Fail("entryStateGUID", std::numeric_limits<u32>::max(), "entry state does not exist");
        else
            graph->entryState = static_cast<u16>(std::distance(graph->states.begin(), entry));

        for (u32 index = 0; index < source.transitions.size(); ++index)
        {
            const EditableGraphTransition& transition = source.transitions[index];
            const auto from = std::find_if(graph->states.begin(), graph->states.end(), [&transition](const CompiledGraphState& value) { return value.sourceGUID == transition.sourceGUID; });
            const auto to = std::find_if(graph->states.begin(), graph->states.end(), [&transition](const CompiledGraphState& value) { return value.sourceGUID == transition.targetGUID; });
            const u64 parameterHash = HashAnimationName(transition.parameter);
            const auto parameter = std::find_if(graph->parameters.begin(), graph->parameters.end(), [parameterHash](const CompiledGraphParameter& value) { return value.nameHash == parameterHash; });
            if (from == graph->states.end() || to == graph->states.end() || from == to || parameter == graph->parameters.end() || !std::isfinite(transition.threshold))
            {
                Fail("transitions", index, "transition reference or threshold is invalid");
                continue;
            }
            if (transition.comparison > GraphComparison::GreaterEqual || transition.curve > BlendCurve::SmoothStep)
            {
                Fail("transitions", index, "transition enum is invalid");
                continue;
            }
            CompiledGraphTransition compiled;
            compiled.sourceState = static_cast<u16>(std::distance(graph->states.begin(), from));
            compiled.targetState = static_cast<u16>(std::distance(graph->states.begin(), to));
            compiled.parameterIndex = static_cast<u16>(std::distance(graph->parameters.begin(), parameter));
            compiled.comparison = transition.comparison;
            compiled.curve = transition.curve;
            compiled.threshold = transition.threshold;
            compiled.durationMicroseconds = transition.durationMicroseconds;
            if (std::any_of(graph->transitions.begin(), graph->transitions.end(), [&compiled](const CompiledGraphTransition& value) { return value.sourceState == compiled.sourceState && value.parameterIndex == compiled.parameterIndex && value.comparison == compiled.comparison && value.threshold == compiled.threshold; }))
                Fail("transitions", index, "ambiguous duplicate transition");
            graph->transitions.push_back(compiled);
        }

        if (source.layers.size() > 4)
            Fail("layers", std::numeric_limits<u32>::max(), "layer count exceeds the fixed pose recipe limit");
        for (u32 index = 0; index < source.layers.size(); ++index)
        {
            const EditableGraphLayer& layer = source.layers[index];
            if (layer.sourceGUID == 0 || layer.clipSlotSemanticID == 0 || layer.durationMicroseconds == 0 || layer.type > AnimationLayerType::Additive || !std::isfinite(layer.constantWeight) || layer.constantWeight < 0.0f || layer.constantWeight > 1.0f)
            {
                Fail("layers", index, "layer identity, clip, duration, type, or weight is invalid");
                continue;
            }
            if (std::any_of(graph->layers.begin(), graph->layers.end(), [&layer](const CompiledGraphLayer& value) { return value.sourceGUID == layer.sourceGUID || value.executionOrder == layer.executionOrder; }))
            {
                Fail("layers", index, "layer GUID or execution order is duplicated");
                continue;
            }
            CompiledGraphLayer compiled;
            compiled.sourceGUID = layer.sourceGUID;
            compiled.clipSlotSemanticID = layer.clipSlotSemanticID;
            compiled.maskAssetID = layer.maskAssetID;
            compiled.durationMicroseconds = layer.durationMicroseconds;
            compiled.type = layer.type;
            compiled.composeInSourceDomain = layer.composeInSourceDomain;
            compiled.constantWeight = layer.constantWeight;
            compiled.executionOrder = layer.executionOrder;
            compiled.emitEvents = layer.emitEvents;
            if (!layer.weightParameter.empty())
            {
                const u64 hash = HashAnimationName(layer.weightParameter);
                const auto parameter = std::find_if(graph->parameters.begin(), graph->parameters.end(), [hash](const CompiledGraphParameter& value) { return value.nameHash == hash; });
                if (parameter == graph->parameters.end() || parameter->type != FileFormat::Animation::GraphValueType::Float)
                {
                    Fail("layers", index, "layer weight parameter is missing or is not a float");
                    continue;
                }
                compiled.weightParameter = static_cast<u16>(std::distance(graph->parameters.begin(), parameter));
            }
            graph->layers.push_back(compiled);
        }
        std::sort(graph->layers.begin(), graph->layers.end(), [](const CompiledGraphLayer& first, const CompiledGraphLayer& second) { return first.executionOrder < second.executionOrder; });
        if (source.actionSlots.size() > source.actionCapacity || source.layers.size() + (source.actionSlots.empty() ? 0u : 1u) > 4)
            Fail("actionSlots", std::numeric_limits<u32>::max(), "action slots exceed the queue or pose recipe limits");
        for (u32 index = 0; index < source.actionSlots.size(); ++index)
        {
            const EditableGraphActionSlot& slot = source.actionSlots[index];
            if (slot.semanticID == 0 || slot.clipSlotSemanticID == 0 || slot.durationMicroseconds == 0 || slot.type > AnimationLayerType::Additive || slot.blendInMicroseconds > slot.durationMicroseconds || slot.blendOutMicroseconds > slot.durationMicroseconds)
            {
                Fail("actionSlots", index, "action semantic, clip, duration, type, or blend interval is invalid");
                continue;
            }
            if (std::any_of(graph->actionSlots.begin(), graph->actionSlots.end(), [&slot](const CompiledGraphActionSlot& value) { return value.semanticID == slot.semanticID; }))
            {
                Fail("actionSlots", index, "action semantic is duplicated");
                continue;
            }
            graph->actionSlots.push_back({slot.semanticID, slot.clipSlotSemanticID, slot.maskAssetID, slot.durationMicroseconds, slot.blendInMicroseconds, slot.blendOutMicroseconds, slot.type, slot.priority, slot.queueWhenBlocked, slot.continueUnderlyingState});
        }
        for (u32 index = 0; index < source.outputs.size(); ++index)
        {
            const EditableGraphOutput& output = source.outputs[index];
            const u64 parameterHash = HashAnimationName(output.parameter);
            const auto parameter = std::find_if(graph->parameters.begin(), graph->parameters.end(), [parameterHash](const CompiledGraphParameter& value) { return value.nameHash == parameterHash; });
            if (output.semanticID == 0 || parameter == graph->parameters.end())
            {
                Fail("outputs", index, "output semantic or parameter is invalid");
                continue;
            }
            if (std::any_of(graph->outputs.begin(), graph->outputs.end(), [&output](const CompiledGraphOutput& value) { return value.semanticID == output.semanticID; }))
            {
                Fail("outputs", index, "output semantic is duplicated");
                continue;
            }
            graph->outputs.push_back({output.semanticID, static_cast<u16>(std::distance(graph->parameters.begin(), parameter))});
        }
        if (source.ikOperations.size() > 4)
            Fail("ikOperations", std::numeric_limits<u32>::max(), "IK operation count exceeds the fixed pose recipe limit");
        for (u32 index = 0; index < source.ikOperations.size(); ++index)
        {
            const EditableGraphIKOperation& operation = source.ikOperations[index];
            const u64 goalHash = HashAnimationName(operation.goalParameter);
            const auto goal = std::find_if(graph->parameters.begin(), graph->parameters.end(), [goalHash](const CompiledGraphParameter& value) { return value.nameHash == goalHash; });
            if (operation.sourceGUID == 0 || operation.rigAssetID == FileFormat::INVALID_ASSET_ID || operation.solverSemanticID == 0 || goal == graph->parameters.end() || goal->type != FileFormat::Animation::GraphValueType::Float3 || !std::isfinite(operation.constantWeight) || operation.constantWeight < 0.0f || operation.constantWeight > 1.0f)
            {
                Fail("ikOperations", index, "IK identity, rig, solver, goal, or weight is invalid");
                continue;
            }
            if (std::any_of(graph->ikOperations.begin(), graph->ikOperations.end(), [&operation](const CompiledGraphIKOperation& value) { return value.sourceGUID == operation.sourceGUID || value.executionOrder == operation.executionOrder; }))
            {
                Fail("ikOperations", index, "IK GUID or execution order is duplicated");
                continue;
            }
            CompiledGraphIKOperation compiled;
            compiled.sourceGUID = operation.sourceGUID;
            compiled.rigAssetID = operation.rigAssetID;
            compiled.solverSemanticID = operation.solverSemanticID;
            compiled.goalParameter = static_cast<u16>(std::distance(graph->parameters.begin(), goal));
            compiled.goalAttachmentSemanticID = operation.goalAttachmentSemanticID;
            compiled.effectorAttachmentSemanticID = operation.effectorAttachmentSemanticID;
            compiled.effectorAlignmentAxis = operation.effectorAlignmentAxis;
            compiled.constantWeight = operation.constantWeight;
            compiled.executionOrder = operation.executionOrder;
            if (!operation.weightParameter.empty())
            {
                const u64 weightHash = HashAnimationName(operation.weightParameter);
                const auto weight = std::find_if(graph->parameters.begin(), graph->parameters.end(), [weightHash](const CompiledGraphParameter& value) { return value.nameHash == weightHash; });
                if (weight == graph->parameters.end() || weight->type != FileFormat::Animation::GraphValueType::Float)
                {
                    Fail("ikOperations", index, "IK weight parameter is missing or is not a float");
                    continue;
                }
                compiled.weightParameter = static_cast<u16>(std::distance(graph->parameters.begin(), weight));
            }
            graph->ikOperations.push_back(compiled);
        }
        std::sort(graph->ikOperations.begin(), graph->ikOperations.end(), [](const CompiledGraphIKOperation& first, const CompiledGraphIKOperation& second) { return first.executionOrder < second.executionOrder; });

        if (!graph->states.empty() && graph->entryState < graph->states.size())
        {
            std::vector<bool> reachable(graph->states.size(), false);
            std::queue<u16> pending;
            pending.push(graph->entryState);
            reachable[graph->entryState] = true;
            while (!pending.empty())
            {
                const u16 state = pending.front();
                pending.pop();
                for (const CompiledGraphTransition& transition : graph->transitions)
                    if (transition.sourceState == state && !reachable[transition.targetState])
                    {
                        reachable[transition.targetState] = true;
                        pending.push(transition.targetState);
                    }
            }
            for (u32 index = 0; index < reachable.size(); ++index)
                if (!reachable[index])
                    Fail("states", index, "state is unreachable from entry");
        }

        if (!result.diagnostics.empty())
            return result;
        graph->controllerProgram.clear();
        const u32 magic = 0x32475641u;
        const u16 version = source.version;
        Append(graph->controllerProgram, magic);
        Append(graph->controllerProgram, version);
        Append(graph->controllerProgram, graph->entryState);
        const u32 parameterCount = static_cast<u32>(graph->parameters.size());
        const u32 stateCount = static_cast<u32>(graph->states.size());
        const u32 transitionCount = static_cast<u32>(graph->transitions.size());
        const u32 layerCount = static_cast<u32>(graph->layers.size());
        const u32 actionSlotCount = static_cast<u32>(graph->actionSlots.size());
        const u32 outputCount = static_cast<u32>(graph->outputs.size());
        const u32 ikOperationCount = static_cast<u32>(graph->ikOperations.size());
        Append(graph->controllerProgram, parameterCount);
        Append(graph->controllerProgram, stateCount);
        Append(graph->controllerProgram, transitionCount);
        Append(graph->controllerProgram, layerCount);
        Append(graph->controllerProgram, actionSlotCount);
        Append(graph->controllerProgram, outputCount);
        Append(graph->controllerProgram, ikOperationCount);
        Append(graph->controllerProgram, graph->parameterBytes);
        for (const CompiledGraphParameter& value : graph->parameters)
        {
            Append(graph->controllerProgram, value.nameHash);
            Append(graph->controllerProgram, value.byteOffset);
            Append(graph->controllerProgram, static_cast<u8>(value.type));
            Append(graph->controllerProgram, static_cast<u8>(value.oneFrameTrigger ? 1 : 0));
            const u16 reserved = 0;
            Append(graph->controllerProgram, reserved);
        }
        for (const CompiledGraphState& value : graph->states)
        {
            Append(graph->controllerProgram, value.sourceGUID);
            Append(graph->controllerProgram, value.clipSlotSemanticID);
            Append(graph->controllerProgram, value.durationMicroseconds);
            Append(graph->controllerProgram, static_cast<u8>(value.kind));
            Append(graph->controllerProgram, value.inputXParameter);
            Append(graph->controllerProgram, value.inputYParameter);
            Append(graph->controllerProgram, value.blendSampleOffset);
            Append(graph->controllerProgram, value.blendSampleCount);
            Append(graph->controllerProgram, value.blendTriangleOffset);
            Append(graph->controllerProgram, value.blendTriangleCount);
            Append(graph->controllerProgram, value.smoothingHalfLifeSeconds);
            Append(graph->controllerProgram, static_cast<u8>(value.wrapX ? 1 : 0));
            Append(graph->controllerProgram, value.wrapMinimum);
            Append(graph->controllerProgram, value.wrapMaximum);
        }
        for (const CompiledGraphBlendSample& value : graph->blendSamples)
        {
            Append(graph->controllerProgram, value.position);
            Append(graph->controllerProgram, value.clipSlotSemanticID);
        }
        for (const BlendSpaceTriangle& value : graph->blendTriangles)
            Append(graph->controllerProgram, value);
        for (const CompiledGraphTransition& value : graph->transitions)
        {
            Append(graph->controllerProgram, value.sourceState);
            Append(graph->controllerProgram, value.targetState);
            Append(graph->controllerProgram, value.parameterIndex);
            Append(graph->controllerProgram, static_cast<u8>(value.comparison));
            Append(graph->controllerProgram, static_cast<u8>(value.curve));
            Append(graph->controllerProgram, value.threshold);
            Append(graph->controllerProgram, value.durationMicroseconds);
        }
        for (const CompiledGraphLayer& value : graph->layers)
        {
            Append(graph->controllerProgram, value.sourceGUID);
            Append(graph->controllerProgram, value.clipSlotSemanticID);
            Append(graph->controllerProgram, value.maskAssetID);
            Append(graph->controllerProgram, value.durationMicroseconds);
            Append(graph->controllerProgram, static_cast<u8>(value.type));
            Append(graph->controllerProgram, value.weightParameter);
            Append(graph->controllerProgram, value.constantWeight);
            Append(graph->controllerProgram, value.executionOrder);
            Append(graph->controllerProgram, static_cast<u8>(value.emitEvents ? 1 : 0));
            Append(graph->controllerProgram, static_cast<u8>(value.composeInSourceDomain ? 1 : 0));
        }
        for (const CompiledGraphActionSlot& value : graph->actionSlots)
        {
            Append(graph->controllerProgram, value.semanticID);
            Append(graph->controllerProgram, value.clipSlotSemanticID);
            Append(graph->controllerProgram, value.maskAssetID);
            Append(graph->controllerProgram, value.durationMicroseconds);
            Append(graph->controllerProgram, value.blendInMicroseconds);
            Append(graph->controllerProgram, value.blendOutMicroseconds);
            Append(graph->controllerProgram, static_cast<u8>(value.type));
            Append(graph->controllerProgram, value.priority);
            Append(graph->controllerProgram, static_cast<u8>(value.queueWhenBlocked ? 1 : 0));
            Append(graph->controllerProgram, static_cast<u8>(value.continueUnderlyingState ? 1 : 0));
        }
        for (const CompiledGraphOutput& value : graph->outputs)
        {
            Append(graph->controllerProgram, value.semanticID);
            Append(graph->controllerProgram, value.parameterIndex);
        }
        for (const CompiledGraphIKOperation& value : graph->ikOperations)
        {
            Append(graph->controllerProgram, value.sourceGUID);
            Append(graph->controllerProgram, value.rigAssetID);
            Append(graph->controllerProgram, value.solverSemanticID);
            Append(graph->controllerProgram, value.goalParameter);
            Append(graph->controllerProgram, value.goalAttachmentSemanticID);
            Append(graph->controllerProgram, value.effectorAttachmentSemanticID);
            Append(graph->controllerProgram, value.effectorAlignmentAxis);
            Append(graph->controllerProgram, value.weightParameter);
            Append(graph->controllerProgram, value.constantWeight);
            Append(graph->controllerProgram, value.executionOrder);
        }
        u64 hash = 0xCBF29CE484222325ull;
        for (u8 byte : graph->controllerProgram)
        {
            hash ^= byte;
            hash *= 0x100000001B3ull;
        }
        graph->contentHash = XXHash64::hash(&graph->maximumPoseDomains, sizeof(graph->maximumPoseDomains), hash);
        graph->contentHash = XXHash64::hash(&graph->retargetChainCorrection, sizeof(graph->retargetChainCorrection), graph->contentHash);
        result.graph = std::move(graph);
        return result;
    }

    struct AnimationControllerStore::Slot
    {
        struct BlendSampleClock
        {
            FileFormat::AssetID clipAssetID = FileFormat::INVALID_ASSET_ID;
            f64 timeMicroseconds = 0.0;
            u64 durationMicroseconds = 0;
            f32 playbackRate = 1.0f;
            bool initialized = false;
            bool active = false;
        };

        struct ActionRecord
        {
            u32 serial = 0;
            u16 slotIndex = 0;
            u64 semanticID = 0;
            FileFormat::AssetID clipAssetID = FileFormat::INVALID_ASSET_ID;
            u32 payload = 0;
            u64 timeMicroseconds = 0;
            AnimationActionStatus status = AnimationActionStatus::Pending;
        };

        struct BoundClip
        {
            FileFormat::AssetID clipAssetID = FileFormat::INVALID_ASSET_ID;
            FileFormat::Animation::PoseDomainType domainType = FileFormat::Animation::PoseDomainType::RigFamily;
            FileFormat::AssetID domainAssetID = FileFormat::INVALID_ASSET_ID;
            u64 durationMicroseconds = 0;
            std::vector<FileFormat::Animation::SynchronizationMarker> synchronizationMarkers;
            std::vector<FileFormat::Animation::AnimationEvent> events;
            std::vector<u8> eventPayloadBytes;
            bool looping = false;
        };

        AnimationControllerConfig config;
        std::vector<u8> parameters;
        std::vector<u32> variations;
        std::vector<AnimationControllerOutput> commands;
        std::vector<AnimationControllerOutput> actions;
        std::vector<ActionRecord> actionRecords;
        std::vector<AnimationEventOutput> authoritativeEvents;
        std::vector<AnimationEventOutput> cosmeticEvents;
        std::vector<AnimationGraphOutput> graphOutputs;
        FileFormat::Animation::AnimationSetAsset boundSetRoot;
        std::vector<FileFormat::Animation::AnimationSetSlot> boundSetSlots;
        std::vector<FileFormat::Animation::AnimationClipVariant> boundSetVariants;
        std::vector<BoundClip> boundClips;
        std::vector<AnimationControllerOutput> outputs;
        std::vector<FileFormat::Animation::LocalTransform> continuitySnapshot;
        std::vector<vec2> smoothedBlendInputs;
        std::vector<FileFormat::AssetID> synchronizationLeaders;
        std::vector<BlendSampleClock> blendSampleClocks;
        u32 generation = 1;
        u32 graphGeneration = 1;
        u32 nextActionSerial = 1;
        u32 activeActionRecord = std::numeric_limits<u32>::max();
        u16 currentState = 0;
        u16 transitionSource = 0;
        u16 transitionTarget = 0;
        u16 transitionIndex = 0;
        u64 stateTimeMicroseconds = 0;
        u64 transitionSourceTimeMicroseconds = 0;
        u64 transitionTimeMicroseconds = 0;
        bool live = false;
        bool transitioning = false;
        bool interruptionNeedsSnapshot = false;
        u32 activeLayers = 0;
        std::array<f32, 4> activeLayerWeights = {};
        u32 activeIKOperations = 0;
        u32 activeBlendSamples = 0;
        u32 activeBlendTriangle = std::numeric_limits<u32>::max();
        std::array<f32, 3> activeBlendWeights = {};
        std::array<f32, 3> synchronizationPlaybackRates = {1.0f, 1.0f, 1.0f};
        FileFormat::AssetID activeSynchronizationLeader = FileFormat::INVALID_ASSET_ID;
        u64 previousSynchronizationMarker = 0;
        u64 nextSynchronizationMarker = 0;
        u64 requestedSlotSemanticID = 0;
        u64 resolvedSlotSemanticID = 0;
        FileFormat::AssetID resolvedClipAssetID = FileFormat::INVALID_ASSET_ID;
        u32 resolvedVariationID = 0;
        u32 resolvedFallbackDepth = 0;
        AnimationSlotResolution slotResolution = AnimationSlotResolution::MissingSlot;
        bool synchronizationFallback = false;
    };

    AnimationControllerStore::AnimationControllerStore() = default;
    AnimationControllerStore::~AnimationControllerStore() = default;

    AnimatorHandle AnimationControllerStore::Create(const AnimationControllerConfig& config)
    {
        if (_updating)
        {
            ++_stats.reentrantMutationRejects;
            return {};
        }
        if (!config.graph || config.targetSkeletonAssetID == FileFormat::INVALID_ASSET_ID)
            return {};
        u32 index = 0;
        if (_freeSlots.empty())
        {
            index = static_cast<u32>(_slots.size());
            _slots.emplace_back();
        }
        else
        {
            index = _freeSlots.back();
            _freeSlots.pop_back();
        }
        Slot& slot = _slots[index];
        slot.live = true;
        slot.config = config;
        slot.parameters = config.graph->defaultParameters;
        slot.variations.assign(config.graph->states.size() + config.graph->blendSamples.size() + config.graph->layers.size() + config.graph->actionSlots.size(), 0);
        slot.commands.clear();
        slot.commands.reserve(config.graph->commandCapacity);
        slot.actions.clear();
        slot.actions.reserve(config.graph->actionCapacity);
        slot.actionRecords.clear();
        slot.actionRecords.reserve(config.graph->actionCapacity);
        slot.authoritativeEvents.clear();
        slot.cosmeticEvents.clear();
        slot.graphOutputs.clear();
        slot.graphOutputs.reserve(config.graph->outputs.size());
        slot.boundSetRoot = {};
        slot.boundSetSlots.clear();
        slot.boundSetVariants.clear();
        slot.boundClips.clear();
        slot.nextActionSerial = 1;
        slot.activeActionRecord = std::numeric_limits<u32>::max();
        slot.outputs.clear();
        slot.outputs.reserve(config.graph->commandCapacity + config.graph->actionCapacity);
        slot.continuitySnapshot.clear();
        slot.continuitySnapshot.reserve(config.graph->maximumSnapshotJoints);
        slot.smoothedBlendInputs.assign(config.graph->states.size(), vec2(0.0f));
        slot.synchronizationLeaders.assign(config.graph->states.size(), FileFormat::INVALID_ASSET_ID);
        slot.blendSampleClocks.assign(config.graph->blendSamples.size(), {});
        for (u32 stateIndex = 0; stateIndex < config.graph->states.size(); ++stateIndex)
        {
            const CompiledGraphState& state = config.graph->states[stateIndex];
            if (state.kind == GraphStateKind::Clip)
                continue;
            const CompiledGraphParameter& x = config.graph->parameters[state.inputXParameter];
            std::memcpy(&slot.smoothedBlendInputs[stateIndex].x, slot.parameters.data() + x.byteOffset, sizeof(f32));
            if (state.kind == GraphStateKind::BlendSpace2D)
            {
                const CompiledGraphParameter& y = config.graph->parameters[state.inputYParameter];
                std::memcpy(&slot.smoothedBlendInputs[stateIndex].y, slot.parameters.data() + y.byteOffset, sizeof(f32));
            }
        }
        slot.currentState = config.graph->entryState;
        slot.transitionSource = slot.currentState;
        slot.transitionTarget = slot.currentState;
        slot.stateTimeMicroseconds = 0;
        slot.transitionSourceTimeMicroseconds = 0;
        slot.transitionTimeMicroseconds = 0;
        slot.transitioning = false;
        slot.interruptionNeedsSnapshot = false;
        return AnimatorHandle::Create(index, slot.generation);
    }

    bool AnimationControllerStore::Destroy(AnimatorHandle handle)
    {
        if (_updating)
        {
            ++_stats.reentrantMutationRejects;
            return false;
        }
        Slot* slot = GetSlot(handle);
        if (!slot)
            return false;
        const u32 index = handle.GetSlot();
        slot->live = false;
        slot->config = {};
        slot->parameters.clear();
        slot->variations.clear();
        slot->commands.clear();
        slot->actions.clear();
        slot->outputs.clear();
        slot->continuitySnapshot.clear();
        slot->smoothedBlendInputs.clear();
        slot->synchronizationLeaders.clear();
        slot->blendSampleClocks.clear();
        if (++slot->generation == 0)
            slot->generation = 1;
        _freeSlots.push_back(index);
        return true;
    }

    AnimationParameterHandle AnimationControllerStore::ResolveParameter(AnimatorHandle handle, u64 nameHash) const
    {
        const Slot* slot = GetSlot(handle);
        if (!slot)
            return {};
        for (u16 index = 0; index < slot->config.graph->parameters.size(); ++index)
        {
            const CompiledGraphParameter& parameter = slot->config.graph->parameters[index];
            if (parameter.nameHash == nameHash)
                return {handle, slot->graphGeneration, index, parameter.type};
        }
        return {};
    }

    bool AnimationControllerStore::ValidateParameter(const AnimationParameterHandle& handle, FileFormat::Animation::GraphValueType type, Slot*& slot)
    {
        slot = GetSlot(handle.animator);
        if (!slot || handle.graphGeneration != slot->graphGeneration || handle.parameterIndex >= slot->config.graph->parameters.size() || handle.type != type || slot->config.graph->parameters[handle.parameterIndex].type != type)
        {
            ++_stats.rejectedWrites;
            return false;
        }
        return true;
    }

    bool AnimationControllerStore::SetBool(AnimationParameterHandle handle, bool value)
    {
        if (_updating)
        {
            ++_stats.reentrantMutationRejects;
            return false;
        }
        Slot* slot = nullptr;
        if (!ValidateParameter(handle, FileFormat::Animation::GraphValueType::Bool, slot))
            return false;
        slot->parameters[slot->config.graph->parameters[handle.parameterIndex].byteOffset] = value ? 1 : 0;
        return true;
    }

    bool AnimationControllerStore::SetInt(AnimationParameterHandle handle, i32 value)
    {
        if (_updating)
        {
            ++_stats.reentrantMutationRejects;
            return false;
        }
        Slot* slot = nullptr;
        if (!ValidateParameter(handle, FileFormat::Animation::GraphValueType::Int, slot))
            return false;
        std::memcpy(slot->parameters.data() + slot->config.graph->parameters[handle.parameterIndex].byteOffset, &value, sizeof(value));
        return true;
    }

    bool AnimationControllerStore::SetFloat(AnimationParameterHandle handle, f32 value)
    {
        if (_updating)
        {
            ++_stats.reentrantMutationRejects;
            return false;
        }
        Slot* slot = nullptr;
        if (!ValidateParameter(handle, FileFormat::Animation::GraphValueType::Float, slot) || !std::isfinite(value))
        {
            if (!std::isfinite(value))
                ++_stats.rejectedWrites;
            return false;
        }
        std::memcpy(slot->parameters.data() + slot->config.graph->parameters[handle.parameterIndex].byteOffset, &value, sizeof(value));
        return true;
    }

    bool AnimationControllerStore::Trigger(AnimationParameterHandle handle)
    {
        if (_updating)
        {
            ++_stats.reentrantMutationRejects;
            return false;
        }
        Slot* slot = nullptr;
        if (!ValidateParameter(handle, FileFormat::Animation::GraphValueType::Bool, slot) || !slot->config.graph->parameters[handle.parameterIndex].oneFrameTrigger)
        {
            if (slot)
                ++_stats.rejectedWrites;
            return false;
        }
        slot->parameters[slot->config.graph->parameters[handle.parameterIndex].byteOffset] = 1;
        return true;
    }

    bool AnimationControllerStore::QueueCommand(AnimatorHandle handle, u64 semanticID, u32 payload)
    {
        if (_updating)
        {
            ++_stats.reentrantMutationRejects;
            return false;
        }
        Slot* slot = GetSlot(handle);
        if (!slot || semanticID == 0 || slot->commands.size() >= slot->config.graph->commandCapacity)
        {
            ++_stats.queueOverflows;
            return false;
        }
        slot->commands.push_back({semanticID, payload});
        return true;
    }

    bool AnimationControllerStore::QueueAction(AnimatorHandle handle, u64 semanticID, u32 payload)
    {
        return static_cast<bool>(StartAction(handle, semanticID, payload));
    }

    AnimationActionToken AnimationControllerStore::StartAction(AnimatorHandle handle, u64 semanticID, u32 payload)
    {
        if (_updating)
        {
            ++_stats.reentrantMutationRejects;
            return {};
        }
        Slot* slot = GetSlot(handle);
        if (!slot || semanticID == 0)
            return {};
        const auto actionSlot = std::find_if(slot->config.graph->actionSlots.begin(), slot->config.graph->actionSlots.end(), [semanticID](const CompiledGraphActionSlot& value) { return value.semanticID == semanticID; });
        if (actionSlot == slot->config.graph->actionSlots.end())
            return {};
        if (slot->actionRecords.size() >= slot->config.graph->actionCapacity)
        {
            const auto terminal = std::find_if(slot->actionRecords.begin(), slot->actionRecords.end(), [](const Slot::ActionRecord& value) { return value.status == AnimationActionStatus::Completed || value.status == AnimationActionStatus::Interrupted || value.status == AnimationActionStatus::Cancelled; });
            if (terminal == slot->actionRecords.end())
            {
                ++_stats.queueOverflows;
                return {};
            }
            const u32 removed = static_cast<u32>(std::distance(slot->actionRecords.begin(), terminal));
            slot->actionRecords.erase(terminal);
            if (slot->activeActionRecord != std::numeric_limits<u32>::max() && slot->activeActionRecord > removed)
                --slot->activeActionRecord;
        }
        u32 serial = slot->nextActionSerial++;
        if (serial == 0)
            serial = slot->nextActionSerial++;
        Slot::ActionRecord record;
        record.serial = serial;
        record.slotIndex = static_cast<u16>(std::distance(slot->config.graph->actionSlots.begin(), actionSlot));
        record.semanticID = semanticID;
        record.payload = payload;
        AnimationLoading::AnimationSetAssetView boundSet = {slot->boundSetRoot, slot->boundSetSlots, slot->boundSetVariants};
        std::vector<AnimationClipDomainRecord> boundDomains;
        boundDomains.reserve(slot->boundClips.size());
        for (const Slot::BoundClip& clip : slot->boundClips)
            boundDomains.push_back({clip.clipAssetID, clip.domainType, clip.domainAssetID, clip.durationMicroseconds, clip.synchronizationMarkers, clip.events, clip.eventPayloadBytes, clip.looping});
        const ResolvedAnimationSlot resolved = ResolveAnimationSlot(boundSet, boundDomains, actionSlot->clipSlotSemanticID, slot->config.stableSeed ^ semanticID ^ serial);
        if (!resolved)
            return {};
        record.clipAssetID = resolved.clipAssetID;
        if (slot->activeActionRecord == std::numeric_limits<u32>::max())
            record.status = AnimationActionStatus::Playing;
        else
        {
            Slot::ActionRecord& active = slot->actionRecords[slot->activeActionRecord];
            const CompiledGraphActionSlot& activeSlot = slot->config.graph->actionSlots[active.slotIndex];
            if (actionSlot->priority > activeSlot.priority)
            {
                active.status = AnimationActionStatus::Interrupted;
                record.status = AnimationActionStatus::Playing;
                slot->activeActionRecord = std::numeric_limits<u32>::max();
            }
            else if (!actionSlot->queueWhenBlocked)
                record.status = AnimationActionStatus::Cancelled;
        }
        slot->actionRecords.push_back(record);
        if (record.status == AnimationActionStatus::Playing)
            slot->activeActionRecord = static_cast<u32>(slot->actionRecords.size() - 1);
        slot->actions.push_back({semanticID, payload});
        return {handle, serial};
    }

    bool AnimationControllerStore::CancelAction(AnimationActionToken token)
    {
        if (_updating)
        {
            ++_stats.reentrantMutationRejects;
            return false;
        }
        Slot* slot = GetSlot(token.animator);
        if (!slot)
            return false;
        const auto found = std::find_if(slot->actionRecords.begin(), slot->actionRecords.end(), [&token](const Slot::ActionRecord& value) { return value.serial == token.serial; });
        if (found == slot->actionRecords.end() || (found->status != AnimationActionStatus::Pending && found->status != AnimationActionStatus::Playing))
            return false;
        const u32 index = static_cast<u32>(std::distance(slot->actionRecords.begin(), found));
        found->status = AnimationActionStatus::Cancelled;
        if (slot->activeActionRecord == index)
            slot->activeActionRecord = std::numeric_limits<u32>::max();
        return true;
    }

    bool AnimationControllerStore::SetFloat3(AnimationParameterHandle handle, const vec3& value)
    {
        if (_updating)
        {
            ++_stats.reentrantMutationRejects;
            return false;
        }
        Slot* slot = nullptr;
        if (!ValidateParameter(handle, FileFormat::Animation::GraphValueType::Float3, slot) || !std::isfinite(value.x) || !std::isfinite(value.y) || !std::isfinite(value.z))
        {
            if (!std::isfinite(value.x) || !std::isfinite(value.y) || !std::isfinite(value.z))
                ++_stats.rejectedWrites;
            return false;
        }
        std::memcpy(slot->parameters.data() + slot->config.graph->parameters[handle.parameterIndex].byteOffset, &value, sizeof(value));
        return true;
    }

    bool AnimationControllerStore::BindAnimationData(AnimatorHandle handle, const AnimationLoading::AnimationSetAssetView& set, std::span<const AnimationClipDomainRecord> clips)
    {
        if (_updating)
        {
            ++_stats.reentrantMutationRejects;
            return false;
        }
        Slot* slot = GetSlot(handle);
        if (!slot)
            return false;
        slot->boundSetRoot = set.root;
        slot->boundSetSlots.assign(set.slots.begin(), set.slots.end());
        slot->boundSetVariants.assign(set.variants.begin(), set.variants.end());
        slot->boundClips.clear();
        slot->boundClips.reserve(clips.size());
        for (const AnimationClipDomainRecord& clip : clips)
        {
            Slot::BoundClip bound;
            bound.clipAssetID = clip.clipAssetID;
            bound.domainType = clip.domainType;
            bound.domainAssetID = clip.domainAssetID;
            bound.durationMicroseconds = clip.durationMicroseconds;
            bound.synchronizationMarkers.assign(clip.synchronizationMarkers.begin(), clip.synchronizationMarkers.end());
            bound.events.assign(clip.events.begin(), clip.events.end());
            bound.eventPayloadBytes.assign(clip.eventPayloadBytes.begin(), clip.eventPayloadBytes.end());
            bound.looping = clip.looping;
            slot->boundClips.push_back(std::move(bound));
        }
        return true;
    }

    bool AnimationControllerStore::GetActionStatus(AnimationActionToken token, AnimationActionStatus& status) const
    {
        const Slot* slot = GetSlot(token.animator);
        if (!slot)
            return false;
        const auto found = std::find_if(slot->actionRecords.begin(), slot->actionRecords.end(), [&token](const Slot::ActionRecord& value) { return value.serial == token.serial; });
        if (found == slot->actionRecords.end())
            return false;
        status = found->status;
        return true;
    }

    u32 AnimationControllerStore::ApplyBatchInputs(std::span<const AnimationControllerBatchInput> inputs)
    {
        if (_updating)
        {
            ++_stats.reentrantMutationRejects;
            return 0;
        }
        u32 applied = 0;
        for (const AnimationControllerBatchInput& input : inputs)
        {
            Slot* slot = GetSlot(input.animator);
            if (!slot)
                continue;
            bool valid = true;
            if (input.speed)
                valid &= SetFloat(input.speed, input.speedValue);
            if (input.grounded)
                valid &= SetBool(input.grounded, input.groundedValue);
            if (input.direction)
                valid &= SetFloat(input.direction, input.directionValue);
            applied += valid ? 1u : 0u;
        }
        return applied;
    }

    void AnimationControllerStore::Advance(f64 deltaSeconds, bool applyTimeScale)
    {
        _updating = true;
        constexpr f64 fixedStepSeconds = 1.0 / 60.0;
        constexpr u64 fixedStepMicroseconds = 16667;
        _accumulatorSeconds += std::max(deltaSeconds, 0.0) * (applyTimeScale ? std::clamp(static_cast<f64>(CVAR_AnimationControllerTimeScale.Get()), 0.0, 4.0) : 1.0);
        const u32 ticks = static_cast<u32>(std::floor((_accumulatorSeconds + 1.0e-12) / fixedStepSeconds));
        _accumulatorSeconds -= static_cast<f64>(ticks) * fixedStepSeconds;
        for (Slot& slot : _slots)
        {
            if (!slot.live)
                continue;
            slot.outputs.clear();
            slot.authoritativeEvents.clear();
            slot.cosmeticEvents.clear();
            slot.outputs.insert(slot.outputs.end(), slot.commands.begin(), slot.commands.end());
            slot.outputs.insert(slot.outputs.end(), slot.actions.begin(), slot.actions.end());
            slot.commands.clear();
            slot.actions.clear();
        }
        for (u32 tick = 0; tick < ticks; ++tick)
        {
            for (Slot& slot : _slots)
            {
                if (!slot.live)
                    continue;
                ++_stats.advances;
                auto EmitEvent = [&](const Slot::BoundClip& clip, const FileFormat::Animation::AnimationEvent& event) {
                    AnimationEventOutput output;
                    output.eventTypeID = event.eventTypeID;
                    output.payloadTypeID = event.payloadTypeID;
                    output.flags = event.flags;
                    output.payload.assign(clip.eventPayloadBytes.begin() + event.payloadOffset, clip.eventPayloadBytes.begin() + event.payloadOffset + event.payloadSize);
                    constexpr size_t eventCapacity = 64;
                    if ((event.flags & 1u) != 0)
                    {
                        if (slot.cosmeticEvents.size() >= eventCapacity)
                            ++_stats.cosmeticEventDrops;
                        else
                            slot.cosmeticEvents.push_back(std::move(output));
                    }
                    else if (slot.authoritativeEvents.size() >= eventCapacity)
                    {
                        ++_stats.authoritativeEventOverflows;
                        ++_stats.queueOverflows;
                    }
                    else
                        slot.authoritativeEvents.push_back(std::move(output));
                };
                auto ScanEvents = [&](const Slot::BoundClip& clip, u64 previousTime, u64 currentTime, bool looping) {
                    if (clip.durationMicroseconds == 0 || currentTime < previousTime)
                        return;
                    for (const FileFormat::Animation::AnimationEvent& event : clip.events)
                    {
                        if (!looping)
                        {
                            if ((event.timeMicroseconds > previousTime && event.timeMicroseconds <= currentTime) || (previousTime == 0 && event.timeMicroseconds == 0))
                                EmitEvent(clip, event);
                            continue;
                        }
                        u64 occurrence = event.timeMicroseconds;
                        if (occurrence == 0 && previousTime == 0)
                            EmitEvent(clip, event);
                        if (occurrence <= previousTime)
                            occurrence += ((previousTime - occurrence) / clip.durationMicroseconds + 1u) * clip.durationMicroseconds;
                        while (occurrence <= currentTime)
                        {
                            EmitEvent(clip, event);
                            if (std::numeric_limits<u64>::max() - occurrence < clip.durationMicroseconds)
                                break;
                            occurrence += clip.durationMicroseconds;
                        }
                    }
                };
                if (slot.activeActionRecord == std::numeric_limits<u32>::max())
                {
                    u32 selected = std::numeric_limits<u32>::max();
                    for (u32 actionIndex = 0; actionIndex < slot.actionRecords.size(); ++actionIndex)
                    {
                        if (slot.actionRecords[actionIndex].status != AnimationActionStatus::Pending)
                            continue;
                        if (selected == std::numeric_limits<u32>::max() || slot.config.graph->actionSlots[slot.actionRecords[actionIndex].slotIndex].priority > slot.config.graph->actionSlots[slot.actionRecords[selected].slotIndex].priority)
                            selected = actionIndex;
                    }
                    if (selected != std::numeric_limits<u32>::max())
                    {
                        slot.actionRecords[selected].status = AnimationActionStatus::Playing;
                        slot.activeActionRecord = selected;
                    }
                }
                bool advanceUnderlying = true;
                if (slot.activeActionRecord != std::numeric_limits<u32>::max())
                {
                    Slot::ActionRecord& action = slot.actionRecords[slot.activeActionRecord];
                    const CompiledGraphActionSlot& actionSlot = slot.config.graph->actionSlots[action.slotIndex];
                    const u64 previousActionTime = action.timeMicroseconds;
                    action.timeMicroseconds += fixedStepMicroseconds;
                    action.timeMicroseconds = std::min(action.timeMicroseconds, actionSlot.durationMicroseconds);
                    const auto clip = std::find_if(slot.boundClips.begin(), slot.boundClips.end(), [&action](const Slot::BoundClip& value) { return value.clipAssetID == action.clipAssetID; });
                    if (clip != slot.boundClips.end())
                        ScanEvents(*clip, previousActionTime, action.timeMicroseconds, false);
                    advanceUnderlying = actionSlot.continueUnderlyingState;
                    if (action.timeMicroseconds >= actionSlot.durationMicroseconds)
                    {
                        action.status = AnimationActionStatus::Completed;
                        slot.activeActionRecord = std::numeric_limits<u32>::max();
                        advanceUnderlying = true;
                    }
                }
                if (!advanceUnderlying)
                    continue;
                for (u32 stateIndex = 0; stateIndex < slot.config.graph->states.size(); ++stateIndex)
                {
                    const CompiledGraphState& state = slot.config.graph->states[stateIndex];
                    if (state.kind == GraphStateKind::Clip)
                        continue;
                    f32 targetX = 0.0f;
                    std::memcpy(&targetX, slot.parameters.data() + slot.config.graph->parameters[state.inputXParameter].byteOffset, sizeof(f32));
                    slot.smoothedBlendInputs[stateIndex].x = AdvanceSmoothedInput(slot.smoothedBlendInputs[stateIndex].x, targetX, state.smoothingHalfLifeSeconds, static_cast<f32>(fixedStepSeconds));
                    if (state.kind == GraphStateKind::BlendSpace2D)
                    {
                        f32 targetY = 0.0f;
                        std::memcpy(&targetY, slot.parameters.data() + slot.config.graph->parameters[state.inputYParameter].byteOffset, sizeof(f32));
                        slot.smoothedBlendInputs[stateIndex].y = AdvanceSmoothedInput(slot.smoothedBlendInputs[stateIndex].y, targetY, state.smoothingHalfLifeSeconds, static_cast<f32>(fixedStepSeconds));
                    }
                }
                for (Slot::BlendSampleClock& clock : slot.blendSampleClocks)
                {
                    if (!clock.active || !clock.initialized || clock.durationMicroseconds == 0)
                        continue;
                    const f64 previousClockTime = clock.timeMicroseconds;
                    const f64 unwrappedClockTime = previousClockTime + static_cast<f64>(fixedStepMicroseconds) * static_cast<f64>(clock.playbackRate);
                    if (clock.clipAssetID == slot.activeSynchronizationLeader)
                    {
                        const auto eventLeader = std::find_if(slot.boundClips.begin(), slot.boundClips.end(), [&clock](const Slot::BoundClip& value) { return value.clipAssetID == clock.clipAssetID; });
                        if (eventLeader != slot.boundClips.end())
                            ScanEvents(*eventLeader, static_cast<u64>(previousClockTime), static_cast<u64>(unwrappedClockTime), true);
                    }
                    clock.timeMicroseconds = std::fmod(unwrappedClockTime, static_cast<f64>(clock.durationMicroseconds));
                }
                const u64 previousStateTime = slot.stateTimeMicroseconds;
                slot.stateTimeMicroseconds += fixedStepMicroseconds;
                const u16 eventStateIndex = slot.transitioning ? slot.transitionTarget : slot.currentState;
                if (eventStateIndex < slot.config.graph->states.size())
                {
                    const CompiledGraphState& eventState = slot.config.graph->states[eventStateIndex];
                    if (eventState.kind == GraphStateKind::Clip)
                    {
                        AnimationLoading::AnimationSetAssetView boundSet = {slot.boundSetRoot, slot.boundSetSlots, slot.boundSetVariants};
                        std::vector<AnimationClipDomainRecord> boundDomains;
                        boundDomains.reserve(slot.boundClips.size());
                        for (const Slot::BoundClip& clip : slot.boundClips)
                            boundDomains.push_back({clip.clipAssetID, clip.domainType, clip.domainAssetID, clip.durationMicroseconds, clip.synchronizationMarkers, clip.events, clip.eventPayloadBytes, clip.looping});
                        const ResolvedAnimationSlot eventLeader = ResolveAnimationSlot(boundSet, boundDomains, eventState.clipSlotSemanticID, slot.config.stableSeed ^ eventState.sourceGUID, slot.variations[eventStateIndex]);
                        if (eventLeader)
                        {
                            slot.variations[eventStateIndex] = eventLeader.stableVariationID;
                            const auto clip = std::find_if(slot.boundClips.begin(), slot.boundClips.end(), [&eventLeader](const Slot::BoundClip& value) { return value.clipAssetID == eventLeader.clipAssetID; });
                            if (clip != slot.boundClips.end())
                                ScanEvents(*clip, previousStateTime, slot.stateTimeMicroseconds, clip->looping);
                        }
                    }
                }
                AnimationLoading::AnimationSetAssetView boundSet = {slot.boundSetRoot, slot.boundSetSlots, slot.boundSetVariants};
                std::vector<AnimationClipDomainRecord> boundDomains;
                boundDomains.reserve(slot.boundClips.size());
                for (const Slot::BoundClip& clip : slot.boundClips)
                    boundDomains.push_back({clip.clipAssetID, clip.domainType, clip.domainAssetID, clip.durationMicroseconds, clip.synchronizationMarkers, clip.events, clip.eventPayloadBytes, clip.looping});
                for (u32 layerIndex = 0; layerIndex < slot.config.graph->layers.size(); ++layerIndex)
                {
                    const CompiledGraphLayer& layer = slot.config.graph->layers[layerIndex];
                    if (!layer.emitEvents)
                        continue;
                    f32 layerWeight = layer.constantWeight;
                    if (layer.weightParameter != std::numeric_limits<u16>::max())
                    {
                        f32 parameterWeight = 0.0f;
                        std::memcpy(&parameterWeight, slot.parameters.data() + slot.config.graph->parameters[layer.weightParameter].byteOffset, sizeof(parameterWeight));
                        layerWeight *= parameterWeight;
                    }
                    if (layerWeight <= 0.0f)
                        continue;
                    const u32 variationIndex = static_cast<u32>(slot.config.graph->states.size() + slot.config.graph->blendSamples.size()) + layerIndex;
                    const ResolvedAnimationSlot eventSource = ResolveAnimationSlot(boundSet, boundDomains, layer.clipSlotSemanticID, slot.config.stableSeed ^ layer.sourceGUID, slot.variations[variationIndex]);
                    const auto clip = std::find_if(slot.boundClips.begin(), slot.boundClips.end(), [&eventSource](const Slot::BoundClip& value) { return value.clipAssetID == eventSource.clipAssetID; });
                    if (clip != slot.boundClips.end())
                        ScanEvents(*clip, previousStateTime, slot.stateTimeMicroseconds, clip->looping);
                }
                if (slot.transitioning)
                {
                    slot.transitionSourceTimeMicroseconds += fixedStepMicroseconds;
                    slot.transitionTimeMicroseconds += fixedStepMicroseconds;
                    const CompiledGraphTransition& active = slot.config.graph->transitions[slot.transitionIndex];
                    if (active.durationMicroseconds == 0 || slot.transitionTimeMicroseconds >= active.durationMicroseconds)
                    {
                        slot.currentState = slot.transitionTarget;
                        slot.transitionSource = slot.currentState;
                        slot.transitioning = false;
                        slot.transitionTimeMicroseconds = 0;
                        slot.continuitySnapshot.clear();
                        slot.interruptionNeedsSnapshot = false;
                    }
                }

                const u16 effectiveState = slot.transitioning ? slot.transitionTarget : slot.currentState;
                for (u16 transitionIndex = 0; transitionIndex < slot.config.graph->transitions.size(); ++transitionIndex)
                {
                    const CompiledGraphTransition& transition = slot.config.graph->transitions[transitionIndex];
                    if (transition.sourceState != effectiveState || transition.targetState == effectiveState)
                        continue;
                    const CompiledGraphParameter& parameter = slot.config.graph->parameters[transition.parameterIndex];
                    f32 value = 0.0f;
                    if (parameter.type == FileFormat::Animation::GraphValueType::Bool)
                        value = slot.parameters[parameter.byteOffset] != 0 ? 1.0f : 0.0f;
                    else if (parameter.type == FileFormat::Animation::GraphValueType::Int)
                    {
                        i32 integer = 0;
                        std::memcpy(&integer, slot.parameters.data() + parameter.byteOffset, sizeof(integer));
                        value = static_cast<f32>(integer);
                    }
                    else
                        std::memcpy(&value, slot.parameters.data() + parameter.byteOffset, sizeof(value));
                    if (!Compare(value, transition.comparison, transition.threshold))
                        continue;
                    if (slot.transitioning)
                    {
                        ++_stats.interruptions;
                        slot.interruptionNeedsSnapshot = true;
                        slot.currentState = effectiveState;
                    }
                    else
                    {
                        slot.transitionSourceTimeMicroseconds = slot.stateTimeMicroseconds;
                    }
                    slot.transitionSource = slot.currentState;
                    slot.transitionTarget = transition.targetState;
                    slot.transitionIndex = transitionIndex;
                    slot.transitionTimeMicroseconds = 0;
                    slot.transitioning = true;
                    slot.stateTimeMicroseconds = 0;
                    const CompiledGraphState& targetState = slot.config.graph->states[slot.transitionTarget];
                    for (u16 localIndex = 0; localIndex < targetState.blendSampleCount; ++localIndex)
                        slot.blendSampleClocks[targetState.blendSampleOffset + localIndex] = {};
                    slot.synchronizationLeaders[slot.transitionTarget] = FileFormat::INVALID_ASSET_ID;
                    ++_stats.transitions;
                    break;
                }
                for (const CompiledGraphParameter& parameter : slot.config.graph->parameters)
                    if (parameter.oneFrameTrigger)
                        slot.parameters[parameter.byteOffset] = 0;
            }
        }
        for (Slot& slot : _slots)
        {
            if (!slot.live)
                continue;
            slot.graphOutputs.clear();
            for (const CompiledGraphOutput& output : slot.config.graph->outputs)
            {
                const CompiledGraphParameter& parameter = slot.config.graph->parameters[output.parameterIndex];
                AnimationGraphOutput value;
                value.semanticID = output.semanticID;
                value.type = parameter.type;
                if (parameter.type == FileFormat::Animation::GraphValueType::Bool)
                    value.value.x = slot.parameters[parameter.byteOffset] != 0 ? 1.0f : 0.0f;
                else if (parameter.type == FileFormat::Animation::GraphValueType::Int)
                {
                    i32 integer = 0;
                    std::memcpy(&integer, slot.parameters.data() + parameter.byteOffset, sizeof(integer));
                    value.value.x = static_cast<f32>(integer);
                }
                else if (parameter.type == FileFormat::Animation::GraphValueType::Float3)
                    std::memcpy(&value.value.x, slot.parameters.data() + parameter.byteOffset, sizeof(vec3));
                else
                    std::memcpy(&value.value.x, slot.parameters.data() + parameter.byteOffset, sizeof(f32));
                slot.graphOutputs.push_back(value);
            }
        }
        _updating = false;
    }

    bool AnimationControllerStore::Seek(AnimatorHandle handle, u64 timeMicroseconds, AnimationSeekEventPolicy eventPolicy)
    {
        if (_updating || eventPolicy != AnimationSeekEventPolicy::ResetWithoutReplay)
        {
            if (_updating)
                ++_stats.reentrantMutationRejects;
            return false;
        }
        Slot* slot = GetSlot(handle);
        if (!slot)
            return false;
        slot->stateTimeMicroseconds = timeMicroseconds;
        slot->transitionSourceTimeMicroseconds = timeMicroseconds;
        slot->transitionTimeMicroseconds = 0;
        slot->transitioning = false;
        slot->continuitySnapshot.clear();
        slot->interruptionNeedsSnapshot = false;
        slot->authoritativeEvents.clear();
        slot->cosmeticEvents.clear();
        slot->synchronizationLeaders.assign(slot->config.graph->states.size(), FileFormat::INVALID_ASSET_ID);
        slot->blendSampleClocks.assign(slot->config.graph->blendSamples.size(), {});
        return true;
    }

    PoseRecipe AnimationControllerStore::RequestPose(AnimatorHandle handle, const AnimationLoading::AnimationSetAssetView& set, std::span<const AnimationClipDomainRecord> clips, std::span<const FileFormat::Animation::LocalTransform> continuityPose)
    {
        PoseRecipe recipe;
        Slot* slot = GetSlot(handle);
        if (!slot)
            return recipe;
        ++_stats.poseRequests;
        if (slot->interruptionNeedsSnapshot && !continuityPose.empty() && continuityPose.size() <= slot->config.graph->maximumSnapshotJoints)
        {
            slot->continuitySnapshot.assign(continuityPose.begin(), continuityPose.end());
            slot->interruptionNeedsSnapshot = false;
            ++_stats.snapshots;
        }
        recipe.sourceState = slot->transitioning ? slot->transitionSource : slot->currentState;
        recipe.targetState = slot->transitioning ? slot->transitionTarget : slot->currentState;
        recipe.usesSnapshot = slot->transitioning && !slot->continuitySnapshot.empty();
        f32 targetWeight = 1.0f;
        if (slot->transitioning)
        {
            const CompiledGraphTransition& transition = slot->config.graph->transitions[slot->transitionIndex];
            const f32 normalized = transition.durationMicroseconds == 0 ? 1.0f : static_cast<f32>(slot->transitionTimeMicroseconds) / static_cast<f32>(transition.durationMicroseconds);
            targetWeight = Curve(transition.curve, normalized);
        }
        recipe.transitionWeight = targetWeight;
        slot->activeBlendSamples = 0;
        slot->activeBlendTriangle = std::numeric_limits<u32>::max();
        slot->activeBlendWeights = {};
        slot->synchronizationPlaybackRates = {1.0f, 1.0f, 1.0f};
        slot->activeSynchronizationLeader = FileFormat::INVALID_ASSET_ID;
        slot->previousSynchronizationMarker = 0;
        slot->nextSynchronizationMarker = 0;
        slot->requestedSlotSemanticID = 0;
        slot->resolvedSlotSemanticID = 0;
        slot->resolvedClipAssetID = FileFormat::INVALID_ASSET_ID;
        slot->resolvedVariationID = 0;
        slot->resolvedFallbackDepth = 0;
        slot->slotResolution = AnimationSlotResolution::MissingSlot;
        slot->synchronizationFallback = false;
        slot->activeLayers = 0;
        slot->activeLayerWeights = {};
        slot->activeIKOperations = 0;
        for (Slot::BlendSampleClock& clock : slot->blendSampleClocks)
            clock.active = false;

        auto AddState = [&](u16 stateIndex, u64 timeMicroseconds, f32 weight) {
            if (recipe.sampleCount >= recipe.samples.size() || weight <= 0.0f || stateIndex >= slot->config.graph->states.size())
                return;
            const CompiledGraphState& state = slot->config.graph->states[stateIndex];
            if (state.kind == GraphStateKind::Clip)
            {
                const ResolvedAnimationSlot resolved = ResolveAnimationSlot(set, clips, state.clipSlotSemanticID, slot->config.stableSeed ^ state.sourceGUID, slot->variations[stateIndex]);
                slot->requestedSlotSemanticID = state.clipSlotSemanticID;
                slot->resolvedSlotSemanticID = resolved.resolvedSemanticID;
                slot->resolvedClipAssetID = resolved.clipAssetID;
                slot->resolvedVariationID = resolved.stableVariationID;
                slot->resolvedFallbackDepth = resolved.fallbackDepth;
                slot->slotResolution = resolved.resolution;
                if (!resolved)
                    return;
                slot->variations[stateIndex] = resolved.stableVariationID;
                recipe.samples[recipe.sampleCount++] = {resolved.clipAssetID, state.durationMicroseconds == 0 ? 0 : timeMicroseconds % state.durationMicroseconds, weight, resolved.stableVariationID};
                return;
            }

            std::array<BlendSpaceSample1D, 64> samples1D = {};
            std::array<BlendSpaceSample2D, 64> samples2D = {};
            for (u16 localIndex = 0; localIndex < state.blendSampleCount; ++localIndex)
            {
                const CompiledGraphBlendSample& sample = slot->config.graph->blendSamples[state.blendSampleOffset + localIndex];
                samples1D[localIndex] = {sample.position.x, sample.clipSlotSemanticID};
                samples2D[localIndex] = {sample.position, sample.clipSlotSemanticID};
            }
            BlendSpaceResult blend;
            if (state.kind == GraphStateKind::BlendSpace1D)
                blend = EvaluateBlendSpace1D(std::span<const BlendSpaceSample1D>(samples1D.data(), state.blendSampleCount), slot->smoothedBlendInputs[stateIndex].x, state.wrapX, state.wrapMinimum, state.wrapMaximum);
            else
                blend = EvaluateBlendSpace2D(std::span<const BlendSpaceSample2D>(samples2D.data(), state.blendSampleCount), std::span<const BlendSpaceTriangle>(slot->config.graph->blendTriangles.data() + state.blendTriangleOffset, state.blendTriangleCount), slot->smoothedBlendInputs[stateIndex], state.wrapX, state.wrapMinimum, state.wrapMaximum);
            slot->activeBlendSamples = blend.activeCount;
            slot->activeBlendTriangle = blend.triangleIndex;
            for (u32 activeIndex = 0; activeIndex < blend.activeCount; ++activeIndex)
                slot->activeBlendWeights[activeIndex] = blend.active[activeIndex].weight;

            std::array<SynchronizationInput, 3> synchronization = {};
            std::array<u32, 3> recipeIndices = {};
            std::array<u32, 3> clockIndices = {};
            u32 synchronizationCount = 0;
            for (u32 activeIndex = 0; activeIndex < blend.activeCount && recipe.sampleCount < recipe.samples.size(); ++activeIndex)
            {
                const u16 localSampleIndex = blend.active[activeIndex].sampleIndex;
                const CompiledGraphBlendSample& sample = slot->config.graph->blendSamples[state.blendSampleOffset + localSampleIndex];
                const u32 variationIndex = static_cast<u32>(slot->config.graph->states.size()) + state.blendSampleOffset + localSampleIndex;
                const ResolvedAnimationSlot resolved = ResolveAnimationSlot(set, clips, sample.clipSlotSemanticID, slot->config.stableSeed ^ state.sourceGUID ^ sample.clipSlotSemanticID, slot->variations[variationIndex]);
                if (activeIndex == 0)
                {
                    slot->requestedSlotSemanticID = sample.clipSlotSemanticID;
                    slot->resolvedSlotSemanticID = resolved.resolvedSemanticID;
                    slot->resolvedClipAssetID = resolved.clipAssetID;
                    slot->resolvedVariationID = resolved.stableVariationID;
                    slot->resolvedFallbackDepth = resolved.fallbackDepth;
                    slot->slotResolution = resolved.resolution;
                }
                if (!resolved)
                    continue;
                slot->variations[variationIndex] = resolved.stableVariationID;
                const auto domain = std::find_if(clips.begin(), clips.end(), [&resolved](const AnimationClipDomainRecord& value) { return value.clipAssetID == resolved.clipAssetID; });
                if (domain == clips.end() || domain->durationMicroseconds == 0)
                    continue;
                const u32 recipeIndex = recipe.sampleCount++;
                const u32 clockIndex = state.blendSampleOffset + localSampleIndex;
                Slot::BlendSampleClock& clock = slot->blendSampleClocks[clockIndex];
                if (clock.clipAssetID != resolved.clipAssetID || clock.durationMicroseconds != domain->durationMicroseconds)
                {
                    clock = {};
                    clock.clipAssetID = resolved.clipAssetID;
                    clock.durationMicroseconds = domain->durationMicroseconds;
                }
                const u64 sampleTime = clock.initialized ? static_cast<u64>(clock.timeMicroseconds) % domain->durationMicroseconds : timeMicroseconds % domain->durationMicroseconds;
                recipe.samples[recipeIndex] = {resolved.clipAssetID, sampleTime, weight * blend.active[activeIndex].weight, resolved.stableVariationID};
                synchronization[synchronizationCount] = {resolved.clipAssetID, blend.active[activeIndex].weight, sampleTime, domain->durationMicroseconds, domain->synchronizationMarkers};
                recipeIndices[synchronizationCount] = recipeIndex;
                clockIndices[synchronizationCount] = clockIndex;
                ++synchronizationCount;
            }
            SynchronizationResult synchronized = SynchronizeSamples(std::span<const SynchronizationInput>(synchronization.data(), synchronizationCount), slot->synchronizationLeaders[stateIndex], 0.1f, 0.2f, 0.25f, 4.0f);
            bool initializedClock = false;
            for (u32 index = 0; index < synchronizationCount; ++index)
            {
                Slot::BlendSampleClock& clock = slot->blendSampleClocks[clockIndices[index]];
                if (clock.initialized)
                    continue;
                clock.timeMicroseconds = static_cast<f64>(synchronized.mappedTimesMicroseconds[index]);
                clock.initialized = true;
                synchronization[index].timeMicroseconds = synchronized.mappedTimesMicroseconds[index];
                initializedClock = true;
            }
            if (initializedClock && synchronized.leaderIndex < synchronizationCount)
                synchronized = SynchronizeSamples(std::span<const SynchronizationInput>(synchronization.data(), synchronizationCount), synchronization[synchronized.leaderIndex].clipAssetID, 0.1f, 0.2f, 0.25f, 4.0f);
            if (synchronized.leaderIndex < synchronizationCount)
            {
                slot->synchronizationLeaders[stateIndex] = synchronization[synchronized.leaderIndex].clipAssetID;
                slot->activeSynchronizationLeader = slot->synchronizationLeaders[stateIndex];
            }
            slot->synchronizationFallback = synchronized.normalizedPhaseFallback || synchronized.degenerateMarkerFallback;
            slot->synchronizationPlaybackRates = synchronized.playbackRates;
            slot->previousSynchronizationMarker = synchronized.previousMarkerSemanticID;
            slot->nextSynchronizationMarker = synchronized.nextMarkerSemanticID;
            for (u32 index = 0; index < synchronizationCount; ++index)
            {
                Slot::BlendSampleClock& clock = slot->blendSampleClocks[clockIndices[index]];
                clock.active = true;
                clock.playbackRate = index == synchronized.leaderIndex ? 1.0f : synchronized.playbackRates[index];
                recipe.samples[recipeIndices[index]].timeMicroseconds = static_cast<u64>(clock.timeMicroseconds) % clock.durationMicroseconds;
            }
        };
        if (slot->transitioning && !recipe.usesSnapshot)
            AddState(slot->transitionSource, slot->transitionSourceTimeMicroseconds, 1.0f - targetWeight);
        AddState(recipe.targetState, slot->stateTimeMicroseconds, slot->transitioning ? targetWeight : 1.0f);
        for (u32 layerIndex = 0; layerIndex < slot->config.graph->layers.size() && recipe.layerCount < recipe.layers.size(); ++layerIndex)
        {
            const CompiledGraphLayer& layer = slot->config.graph->layers[layerIndex];
            f32 layerWeight = layer.constantWeight;
            if (layer.weightParameter != std::numeric_limits<u16>::max())
            {
                const CompiledGraphParameter& parameter = slot->config.graph->parameters[layer.weightParameter];
                std::memcpy(&layerWeight, slot->parameters.data() + parameter.byteOffset, sizeof(layerWeight));
                layerWeight *= layer.constantWeight;
            }
            layerWeight = std::clamp(layerWeight, 0.0f, 1.0f);
            if (layerWeight <= 0.0f)
                continue;
            const u32 variationIndex = static_cast<u32>(slot->config.graph->states.size() + slot->config.graph->blendSamples.size()) + layerIndex;
            const ResolvedAnimationSlot resolved = ResolveAnimationSlot(set, clips, layer.clipSlotSemanticID, slot->config.stableSeed ^ layer.sourceGUID, slot->variations[variationIndex]);
            if (!resolved)
                continue;
            slot->variations[variationIndex] = resolved.stableVariationID;
            PoseRecipeLayer& recipeLayer = recipe.layers[recipe.layerCount++];
            recipeLayer.sample = {resolved.clipAssetID, slot->stateTimeMicroseconds % layer.durationMicroseconds, 1.0f, resolved.stableVariationID};
            recipeLayer.maskAssetID = layer.maskAssetID;
            recipeLayer.weight = layerWeight;
            recipeLayer.type = layer.type;
            recipeLayer.composeInSourceDomain = layer.composeInSourceDomain;
            recipeLayer.executionOrder = layer.executionOrder;
            slot->activeLayerWeights[slot->activeLayers++] = layerWeight;
        }
        if (slot->activeActionRecord != std::numeric_limits<u32>::max() && recipe.layerCount < recipe.layers.size())
        {
            Slot::ActionRecord& action = slot->actionRecords[slot->activeActionRecord];
            const CompiledGraphActionSlot& actionSlot = slot->config.graph->actionSlots[action.slotIndex];
            const u32 variationIndex = static_cast<u32>(slot->config.graph->states.size() + slot->config.graph->blendSamples.size() + slot->config.graph->layers.size()) + action.slotIndex;
            if (variationIndex >= slot->variations.size())
                slot->variations.resize(variationIndex + 1, 0);
            const ResolvedAnimationSlot resolved = ResolveAnimationSlot(set, clips, actionSlot.clipSlotSemanticID, slot->config.stableSeed ^ actionSlot.semanticID ^ action.serial, slot->variations[variationIndex]);
            if (resolved)
            {
                slot->variations[variationIndex] = resolved.stableVariationID;
                f32 weight = 1.0f;
                if (actionSlot.blendInMicroseconds != 0)
                    weight = std::min(weight, static_cast<f32>(action.timeMicroseconds) / static_cast<f32>(actionSlot.blendInMicroseconds));
                if (actionSlot.blendOutMicroseconds != 0)
                    weight = std::min(weight, static_cast<f32>(actionSlot.durationMicroseconds - action.timeMicroseconds) / static_cast<f32>(actionSlot.blendOutMicroseconds));
                PoseRecipeLayer& recipeLayer = recipe.layers[recipe.layerCount++];
                recipeLayer.sample = {resolved.clipAssetID, action.timeMicroseconds, 1.0f, resolved.stableVariationID};
                recipeLayer.maskAssetID = actionSlot.maskAssetID;
                recipeLayer.weight = std::clamp(weight, 0.0f, 1.0f);
                recipeLayer.type = actionSlot.type;
                recipeLayer.executionOrder = std::numeric_limits<u16>::max();
                slot->activeLayerWeights[slot->activeLayers++] = recipeLayer.weight;
            }
        }
        for (const CompiledGraphIKOperation& operation : slot->config.graph->ikOperations)
        {
            f32 weight = operation.constantWeight;
            if (operation.weightParameter != std::numeric_limits<u16>::max())
            {
                const CompiledGraphParameter& parameter = slot->config.graph->parameters[operation.weightParameter];
                f32 parameterWeight = 0.0f;
                std::memcpy(&parameterWeight, slot->parameters.data() + parameter.byteOffset, sizeof(parameterWeight));
                weight *= parameterWeight;
            }
            weight = std::clamp(weight, 0.0f, 1.0f);
            if (weight <= 0.0f || recipe.ikOperationCount >= recipe.ikOperations.size())
                continue;
            const CompiledGraphParameter& goalParameter = slot->config.graph->parameters[operation.goalParameter];
            PoseRecipeIKOperation& recipeOperation = recipe.ikOperations[recipe.ikOperationCount++];
            recipeOperation.rigAssetID = operation.rigAssetID;
            recipeOperation.solverSemanticID = operation.solverSemanticID;
            std::memcpy(&recipeOperation.goalModel, slot->parameters.data() + goalParameter.byteOffset, sizeof(vec3));
            recipeOperation.goalAttachmentSemanticID = operation.goalAttachmentSemanticID;
            recipeOperation.effectorAttachmentSemanticID = operation.effectorAttachmentSemanticID;
            recipeOperation.effectorAlignmentAxis = operation.effectorAlignmentAxis;
            recipeOperation.weight = weight;
            recipeOperation.executionOrder = operation.executionOrder;
            ++slot->activeIKOperations;
        }
        return recipe;
    }

    bool AnimationControllerStore::GetState(AnimatorHandle handle, AnimationControllerState& state) const
    {
        const Slot* slot = GetSlot(handle);
        if (!slot)
            return false;
        state.currentState = slot->currentState;
        state.transitionTarget = slot->transitionTarget;
        state.stateTimeMicroseconds = slot->stateTimeMicroseconds;
        state.transitionTimeMicroseconds = slot->transitionTimeMicroseconds;
        state.transitionDurationMicroseconds = slot->transitioning ? slot->config.graph->transitions[slot->transitionIndex].durationMicroseconds : 0;
        state.transitioning = slot->transitioning;
        state.hasContinuitySnapshot = !slot->continuitySnapshot.empty();
        state.activeBlendSamples = slot->activeBlendSamples;
        state.activeBlendTriangle = slot->activeBlendTriangle;
        state.activeBlendWeights = slot->activeBlendWeights;
        state.synchronizationPlaybackRates = slot->synchronizationPlaybackRates;
        state.synchronizationLeader = slot->activeSynchronizationLeader;
        state.previousSynchronizationMarker = slot->previousSynchronizationMarker;
        state.nextSynchronizationMarker = slot->nextSynchronizationMarker;
        state.requestedSlotSemanticID = slot->requestedSlotSemanticID;
        state.resolvedSlotSemanticID = slot->resolvedSlotSemanticID;
        state.resolvedClipAssetID = slot->resolvedClipAssetID;
        state.resolvedVariationID = slot->resolvedVariationID;
        state.resolvedFallbackDepth = slot->resolvedFallbackDepth;
        state.slotResolution = slot->slotResolution;
        state.synchronizationFallback = slot->synchronizationFallback;
        state.activeLayers = slot->activeLayers;
        state.activeLayerWeights = slot->activeLayerWeights;
        state.activeIKOperations = slot->activeIKOperations;
        state.activeActionSemanticID = 0;
        state.activeActionTimeMicroseconds = 0;
        state.activeActionStatus = AnimationActionStatus::Completed;
        if (slot->activeActionRecord != std::numeric_limits<u32>::max())
        {
            const Slot::ActionRecord& action = slot->actionRecords[slot->activeActionRecord];
            state.activeActionSemanticID = action.semanticID;
            state.activeActionTimeMicroseconds = action.timeMicroseconds;
            state.activeActionStatus = action.status;
        }
        return true;
    }

    bool AnimationControllerStore::GetParameterValues(AnimatorHandle handle, std::vector<vec3>& values) const
    {
        const Slot* slot = GetSlot(handle);
        if (!slot)
            return false;
        values.clear();
        values.reserve(slot->config.graph->parameters.size());
        for (const CompiledGraphParameter& parameter : slot->config.graph->parameters)
        {
            if (parameter.type == FileFormat::Animation::GraphValueType::Bool)
                values.emplace_back(slot->parameters[parameter.byteOffset] != 0 ? 1.0f : 0.0f, 0.0f, 0.0f);
            else if (parameter.type == FileFormat::Animation::GraphValueType::Int)
            {
                i32 value = 0;
                std::memcpy(&value, slot->parameters.data() + parameter.byteOffset, sizeof(value));
                values.emplace_back(static_cast<f32>(value), 0.0f, 0.0f);
            }
            else if (parameter.type == FileFormat::Animation::GraphValueType::Float3)
            {
                vec3 value = {};
                std::memcpy(&value, slot->parameters.data() + parameter.byteOffset, sizeof(value));
                values.push_back(value);
            }
            else
            {
                f32 value = 0.0f;
                std::memcpy(&value, slot->parameters.data() + parameter.byteOffset, sizeof(value));
                values.emplace_back(value, 0.0f, 0.0f);
            }
        }
        return true;
    }

    bool AnimationControllerStore::GetBoundsMetadata(AnimatorHandle handle, const AnimationLoading::AnimationSetAssetView& set, std::span<const AnimationClipDomainRecord> clips, FileFormat::AssetID& clipAssetID, u64& timeMicroseconds, bool& supportsTightBounds)
    {
        Slot* slot = GetSlot(handle);
        clipAssetID = FileFormat::INVALID_ASSET_ID;
        timeMicroseconds = 0;
        supportsTightBounds = false;
        if (!slot || slot->transitioning || slot->currentState >= slot->config.graph->states.size())
            return slot != nullptr;
        for (const CompiledGraphLayer& layer : slot->config.graph->layers)
        {
            f32 weight = layer.constantWeight;
            if (layer.weightParameter != std::numeric_limits<u16>::max())
            {
                const CompiledGraphParameter& parameter = slot->config.graph->parameters[layer.weightParameter];
                std::memcpy(&weight, slot->parameters.data() + parameter.byteOffset, sizeof(weight));
                weight *= layer.constantWeight;
            }
            if (weight > 0.0f)
                return true;
        }
        for (const CompiledGraphIKOperation& operation : slot->config.graph->ikOperations)
        {
            f32 weight = operation.constantWeight;
            if (operation.weightParameter != std::numeric_limits<u16>::max())
            {
                const CompiledGraphParameter& parameter = slot->config.graph->parameters[operation.weightParameter];
                std::memcpy(&weight, slot->parameters.data() + parameter.byteOffset, sizeof(weight));
                weight *= operation.constantWeight;
            }
            if (weight > 0.0f)
                return true;
        }
        const CompiledGraphState& state = slot->config.graph->states[slot->currentState];
        if (state.kind != GraphStateKind::Clip)
            return true;
        const ResolvedAnimationSlot resolved = ResolveAnimationSlot(set, clips, state.clipSlotSemanticID, slot->config.stableSeed ^ state.sourceGUID, slot->variations[slot->currentState]);
        if (!resolved)
            return true;
        slot->variations[slot->currentState] = resolved.stableVariationID;
        clipAssetID = resolved.clipAssetID;
        timeMicroseconds = state.durationMicroseconds == 0 ? 0 : slot->stateTimeMicroseconds % state.durationMicroseconds;
        supportsTightBounds = true;
        return true;
    }

    std::span<const AnimationControllerOutput> AnimationControllerStore::GetOutputs(AnimatorHandle handle) const
    {
        const Slot* slot = GetSlot(handle);
        return slot ? std::span<const AnimationControllerOutput>(slot->outputs) : std::span<const AnimationControllerOutput>();
    }

    std::span<const AnimationEventOutput> AnimationControllerStore::GetAuthoritativeEvents(AnimatorHandle handle) const
    {
        const Slot* slot = GetSlot(handle);
        return slot ? std::span<const AnimationEventOutput>(slot->authoritativeEvents) : std::span<const AnimationEventOutput>();
    }

    std::span<const AnimationEventOutput> AnimationControllerStore::GetCosmeticEvents(AnimatorHandle handle) const
    {
        const Slot* slot = GetSlot(handle);
        return slot ? std::span<const AnimationEventOutput>(slot->cosmeticEvents) : std::span<const AnimationEventOutput>();
    }

    std::span<const AnimationGraphOutput> AnimationControllerStore::GetGraphOutputs(AnimatorHandle handle) const
    {
        const Slot* slot = GetSlot(handle);
        return slot ? std::span<const AnimationGraphOutput>(slot->graphOutputs) : std::span<const AnimationGraphOutput>();
    }

    void AnimationControllerStore::ClearOutputs(AnimatorHandle handle)
    {
        Slot* slot = GetSlot(handle);
        if (slot)
            slot->outputs.clear();
    }

    bool AnimationControllerStore::ReplaceGraph(AnimatorHandle handle, FileFormat::AssetID graphAssetID, std::shared_ptr<const CompiledAnimationGraph> graph)
    {
        if (_updating)
        {
            ++_stats.reentrantMutationRejects;
            return false;
        }
        Slot* slot = GetSlot(handle);
        if (!slot || !graph)
            return false;
        std::vector<u8> parameters = graph->defaultParameters;
        for (const CompiledGraphParameter& replacement : graph->parameters)
        {
            const auto old = std::find_if(slot->config.graph->parameters.begin(), slot->config.graph->parameters.end(), [&replacement](const CompiledGraphParameter& value) { return value.nameHash == replacement.nameHash && value.type == replacement.type; });
            if (old == slot->config.graph->parameters.end())
                continue;
            const u32 size = ValueSize(replacement.type);
            std::memcpy(parameters.data() + replacement.byteOffset, slot->parameters.data() + old->byteOffset, size);
        }
        slot->config.graph = std::move(graph);
        slot->config.graphAssetID = graphAssetID;
        slot->parameters = std::move(parameters);
        slot->variations.assign(slot->config.graph->states.size() + slot->config.graph->blendSamples.size(), 0);
        slot->smoothedBlendInputs.assign(slot->config.graph->states.size(), vec2(0.0f));
        slot->synchronizationLeaders.assign(slot->config.graph->states.size(), FileFormat::INVALID_ASSET_ID);
        slot->blendSampleClocks.assign(slot->config.graph->blendSamples.size(), {});
        slot->currentState = slot->config.graph->entryState;
        slot->stateTimeMicroseconds = 0;
        slot->transitionSourceTimeMicroseconds = 0;
        slot->transitionTimeMicroseconds = 0;
        slot->transitioning = false;
        slot->continuitySnapshot.clear();
        slot->interruptionNeedsSnapshot = false;
        slot->authoritativeEvents.clear();
        slot->cosmeticEvents.clear();
        for (Slot::ActionRecord& action : slot->actionRecords)
        {
            if (action.status == AnimationActionStatus::Pending || action.status == AnimationActionStatus::Playing)
                action.status = AnimationActionStatus::Interrupted;
        }
        slot->activeActionRecord = std::numeric_limits<u32>::max();
        if (++slot->graphGeneration == 0)
            slot->graphGeneration = 1;
        return true;
    }

    AnimationControllerStats AnimationControllerStore::GetStats() const
    {
        AnimationControllerStats result = _stats;
        result.liveControllers = static_cast<u32>(_slots.size() - _freeSlots.size());
        return result;
    }

    AnimationControllerStore::Slot* AnimationControllerStore::GetSlot(AnimatorHandle handle)
    {
        if (!handle || handle.GetSlot() >= _slots.size())
        {
            ++_stats.staleHandleRejects;
            return nullptr;
        }
        Slot& slot = _slots[handle.GetSlot()];
        if (!slot.live || slot.generation != handle.GetGeneration())
        {
            ++_stats.staleHandleRejects;
            return nullptr;
        }
        return &slot;
    }

    const AnimationControllerStore::Slot* AnimationControllerStore::GetSlot(AnimatorHandle handle) const
    {
        if (!handle || handle.GetSlot() >= _slots.size())
        {
            ++_stats.staleHandleRejects;
            return nullptr;
        }
        const Slot& slot = _slots[handle.GetSlot()];
        if (!slot.live || slot.generation != handle.GetGeneration())
        {
            ++_stats.staleHandleRejects;
            return nullptr;
        }
        return &slot;
    }
} // namespace Animation
