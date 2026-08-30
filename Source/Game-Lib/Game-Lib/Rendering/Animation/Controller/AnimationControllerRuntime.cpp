#include "AnimationControllerRuntime.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <queue>

namespace
{
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
            else
                std::memcpy(graph->defaultParameters.data() + parameterOffset, &parameter.defaultFloat, sizeof(f32));
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
        Append(graph->controllerProgram, parameterCount);
        Append(graph->controllerProgram, stateCount);
        Append(graph->controllerProgram, transitionCount);
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
        u64 hash = 0xCBF29CE484222325ull;
        for (u8 byte : graph->controllerProgram)
        {
            hash ^= byte;
            hash *= 0x100000001B3ull;
        }
        graph->contentHash = hash;
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

        AnimationControllerConfig config;
        std::vector<u8> parameters;
        std::vector<u32> variations;
        std::vector<AnimationControllerOutput> commands;
        std::vector<AnimationControllerOutput> actions;
        std::vector<AnimationControllerOutput> outputs;
        std::vector<FileFormat::Animation::LocalTransform> continuitySnapshot;
        std::vector<vec2> smoothedBlendInputs;
        std::vector<FileFormat::AssetID> synchronizationLeaders;
        std::vector<BlendSampleClock> blendSampleClocks;
        u32 generation = 1;
        u32 graphGeneration = 1;
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
        slot.variations.assign(config.graph->states.size() + config.graph->blendSamples.size(), 0);
        slot.commands.clear();
        slot.commands.reserve(config.graph->commandCapacity);
        slot.actions.clear();
        slot.actions.reserve(config.graph->actionCapacity);
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
        if (_updating)
        {
            ++_stats.reentrantMutationRejects;
            return false;
        }
        Slot* slot = GetSlot(handle);
        if (!slot || semanticID == 0 || slot->actions.size() >= slot->config.graph->actionCapacity)
        {
            ++_stats.queueOverflows;
            return false;
        }
        slot->actions.push_back({semanticID, payload});
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

    void AnimationControllerStore::Advance(f64 deltaSeconds)
    {
        _updating = true;
        constexpr f64 fixedStepSeconds = 1.0 / 60.0;
        constexpr u64 fixedStepMicroseconds = 16667;
        _accumulatorSeconds += std::max(deltaSeconds, 0.0);
        const u32 ticks = static_cast<u32>(std::floor((_accumulatorSeconds + 1.0e-12) / fixedStepSeconds));
        _accumulatorSeconds -= static_cast<f64>(ticks) * fixedStepSeconds;
        for (Slot& slot : _slots)
        {
            if (!slot.live)
                continue;
            slot.outputs.clear();
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
                    clock.timeMicroseconds = std::fmod(clock.timeMicroseconds + static_cast<f64>(fixedStepMicroseconds) * static_cast<f64>(clock.playbackRate), static_cast<f64>(clock.durationMicroseconds));
                }
                slot.stateTimeMicroseconds += fixedStepMicroseconds;
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
        _updating = false;
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
        slot->transitioning = false;
        slot->continuitySnapshot.clear();
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
