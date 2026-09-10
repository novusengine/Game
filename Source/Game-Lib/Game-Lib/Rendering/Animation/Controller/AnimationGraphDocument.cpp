#include "AnimationGraphDocument.h"

#include "Game-Lib/Rendering/Animation/Asset/AnimationAuthoring.h"
#include <algorithm>

namespace Animation
{
    template <typename T, typename F> requires std::is_same_v<std::remove_const_t<T>, BlendSpaceTriangle>
    void VisitMembers(T& value, F&& field) { field("samples", value.samples); }
    template <typename T, typename F> requires std::is_same_v<std::remove_const_t<T>, EditableGraphBlendSample>
    void VisitMembers(T& value, F&& field) { field("position", value.position); field("clipSlotSemanticID", value.clipSlotSemanticID); field("displayName", value.displayName); }
    template <typename T, typename F> requires std::is_same_v<std::remove_const_t<T>, EditableGraphParameter>
    void VisitMembers(T& value, F&& field) { field("name", value.name); field("type", value.type); field("defaultFloat", value.defaultFloat); field("defaultInt", value.defaultInt); field("defaultBool", value.defaultBool); field("oneFrameTrigger", value.oneFrameTrigger); field("defaultFloat3", value.defaultFloat3); field("sourceGUID", value.sourceGUID); field("stateVersion", value.stateVersion); }
    template <typename T, typename F> requires std::is_same_v<std::remove_const_t<T>, EditableGraphState>
    void VisitMembers(T& value, F&& field) { field("sourceGUID", value.sourceGUID); field("clipSlotSemanticID", value.clipSlotSemanticID); field("durationMicroseconds", value.durationMicroseconds); field("kind", value.kind); field("inputX", value.inputX); field("inputY", value.inputY); field("smoothingHalfLifeSeconds", value.smoothingHalfLifeSeconds); field("wrapX", value.wrapX); field("wrapMinimum", value.wrapMinimum); field("wrapMaximum", value.wrapMaximum); field("blendSamples", value.blendSamples); field("blendTriangles", value.blendTriangles); field("displayName", value.displayName); field("editorPosition", value.editorPosition); field("stateVersion", value.stateVersion); }
    template <typename T, typename F> requires std::is_same_v<std::remove_const_t<T>, EditableGraphTransition>
    void VisitMembers(T& value, F&& field) { field("sourceGUID", value.sourceGUID); field("targetGUID", value.targetGUID); field("parameter", value.parameter); field("comparison", value.comparison); field("threshold", value.threshold); field("durationMicroseconds", value.durationMicroseconds); field("curve", value.curve); }
    template <typename T, typename F> requires std::is_same_v<std::remove_const_t<T>, EditableGraphLayer>
    void VisitMembers(T& value, F&& field) { field("sourceGUID", value.sourceGUID); field("clipSlotSemanticID", value.clipSlotSemanticID); field("maskAssetID", value.maskAssetID); field("durationMicroseconds", value.durationMicroseconds); field("type", value.type); field("weightParameter", value.weightParameter); field("constantWeight", value.constantWeight); field("executionOrder", value.executionOrder); field("displayName", value.displayName); field("emitEvents", value.emitEvents); field("composeInSourceDomain", value.composeInSourceDomain); }
    template <typename T, typename F> requires std::is_same_v<std::remove_const_t<T>, EditableGraphActionSlot>
    void VisitMembers(T& value, F&& field) { field("semanticID", value.semanticID); field("clipSlotSemanticID", value.clipSlotSemanticID); field("maskAssetID", value.maskAssetID); field("durationMicroseconds", value.durationMicroseconds); field("blendInMicroseconds", value.blendInMicroseconds); field("blendOutMicroseconds", value.blendOutMicroseconds); field("type", value.type); field("priority", value.priority); field("queueWhenBlocked", value.queueWhenBlocked); field("continueUnderlyingState", value.continueUnderlyingState); field("displayName", value.displayName); }
    template <typename T, typename F> requires std::is_same_v<std::remove_const_t<T>, EditableGraphOutput>
    void VisitMembers(T& value, F&& field) { field("semanticID", value.semanticID); field("parameter", value.parameter); }
    template <typename T, typename F> requires std::is_same_v<std::remove_const_t<T>, EditableGraphIKOperation>
    void VisitMembers(T& value, F&& field) { field("sourceGUID", value.sourceGUID); field("rigAssetID", value.rigAssetID); field("solverSemanticID", value.solverSemanticID); field("goalParameter", value.goalParameter); field("goalAttachmentSemanticID", value.goalAttachmentSemanticID); field("effectorAttachmentSemanticID", value.effectorAttachmentSemanticID); field("effectorAlignmentAxis", value.effectorAlignmentAxis); field("weightParameter", value.weightParameter); field("constantWeight", value.constantWeight); field("executionOrder", value.executionOrder); field("displayName", value.displayName); }
    template <typename T, typename F> requires std::is_same_v<std::remove_const_t<T>, EditableAnimationGraph>
    void VisitMembers(T& value, F&& field) { field("version", value.version); field("parameters", value.parameters); field("states", value.states); field("transitions", value.transitions); field("layers", value.layers); field("actionSlots", value.actionSlots); field("outputs", value.outputs); field("ikOperations", value.ikOperations); field("entryStateGUID", value.entryStateGUID); field("maximumSnapshotJoints", value.maximumSnapshotJoints); field("maximumPoseDomains", value.maximumPoseDomains); field("retargetChainCorrection", value.retargetChainCorrection); field("commandCapacity", value.commandCapacity); field("actionCapacity", value.actionCapacity); field("displayName", value.displayName); }

    AnimationGraphDocument::AnimationGraphDocument(EditableAnimationGraph source) : _source(std::move(source)), _saved(_source)
    {
        for (auto& parameter : _source.parameters)
            if (!parameter.sourceGUID)
                parameter.sourceGUID = HashAnimationName(parameter.name);
        _saved = _source;
        Compile();
    }

    void AnimationGraphDocument::Push(std::deque<Revision>& history)
    {
        constexpr size_t maximumHistory = 32;
        if (history.size() == maximumHistory)
            history.pop_front();
        history.push_back({_source, _revision});
    }

    void AnimationGraphDocument::Edit(EditableAnimationGraph source)
    {
        Push(_undo);
        _redo.clear();
        _source = std::move(source);
        _revision = _nextRevision++;
        _compilation = {};
    }

    bool AnimationGraphDocument::Undo()
    {
        if (_undo.empty())
            return false;
        Push(_redo);
        _source = std::move(_undo.back().source);
        _revision = _undo.back().id;
        _undo.pop_back();
        _compilation = {};
        return true;
    }

    bool AnimationGraphDocument::Redo()
    {
        if (_redo.empty())
            return false;
        Push(_undo);
        _source = std::move(_redo.back().source);
        _revision = _redo.back().id;
        _redo.pop_back();
        _compilation = {};
        return true;
    }

    void AnimationGraphDocument::MarkSaved()
    {
        _saved = _source;
        _savedRevision = _revision;
    }

    void AnimationGraphDocument::Revert()
    {
        Push(_undo);
        _redo.clear();
        _source = _saved;
        _revision = _savedRevision;
        _compilation = {};
    }

    bool AnimationGraphDocument::Compile()
    {
        _compilation = CompileAnimationGraph(_source);
        if (!_compilation)
            return false;
        _lastGoodGraph = _compilation.graph;
        return true;
    }

    bool AnimationGraphDocument::EditField(std::string_view section, u32 index, std::string_view field, std::string_view value, std::string& error)
    {
        error.clear();
        try
        {
            auto source = _source;
            const auto path = section.empty() ? "/" + std::string(field) : "/" + std::string(section) + "/" + std::to_string(index) + "/" + std::string(field);
            Authoring::EditField(source, path, value);
            Edit(std::move(source)); return true;
        }
        catch (const std::exception& exception) { error = exception.what(); return false; }
    }

    bool AnimationGraphDocument::DuplicateItem(std::string_view section, u32 index, std::string& error)
    {
        error.clear();
        try
        {
            auto source = _source;
            bool found = false;
            Authoring::Walk(source, "", [&](const std::string& path, auto& items)
            {
                using V = std::decay_t<decltype(items)>;
                if constexpr (Authoring::IsVector<V>)
                {
                    if (path != "/" + std::string(section)) return;
                    if (items.size() >= 256) throw std::runtime_error("Collection has reached the 256 item limit");
                    using Item = typename V::value_type;
                    Item copy = index < items.size() ? items[index] : Item{};
                    if constexpr (requires { copy.sourceGUID; } && !std::is_same_v<Item, EditableGraphTransition>)
                    {
                        u64 nextGUID = 1;
                        while (std::any_of(items.begin(), items.end(), [nextGUID](const auto& item) { return item.sourceGUID == nextGUID; })) ++nextGUID;
                        copy.sourceGUID = nextGUID;
                    }
                    if constexpr (std::is_same_v<Item, EditableGraphParameter>)
                    {
                        if (copy.name.empty()) copy.name = "parameter";
                        copy.name += "Copy";
                        while (std::any_of(items.begin(), items.end(), [&](const auto& item) { return item.name == copy.name; })) copy.name += "Copy";
                    }
                    if constexpr (std::is_same_v<Item, EditableGraphState>) { copy.displayName += " Copy"; copy.editorPosition.x += 240.0f; }
                    items.push_back(std::move(copy)); found = true;
                }
            });
            if (!found) throw std::runtime_error("Unknown graph collection");
            Edit(std::move(source)); return true;
        }
        catch (const std::exception& exception) { error = exception.what(); return false; }
    }

    bool AnimationGraphDocument::RemoveItem(std::string_view section, u32 index, std::string& error)
    {
        error.clear();
        try
        {
            auto source = _source;
            bool found = false;
            Authoring::Walk(source, "", [&](const std::string& path, auto& items)
            {
                using V = std::decay_t<decltype(items)>;
                if constexpr (Authoring::IsVector<V>)
                {
                    if (path != "/" + std::string(section)) return;
                    if (index >= items.size()) throw std::runtime_error("Collection index is out of range");
                    using Item = typename V::value_type;
                    if constexpr (std::is_same_v<Item, EditableGraphState>)
                    {
                        const auto removed = items[index].sourceGUID;
                        std::erase_if(source.transitions, [removed](const auto& transition) { return transition.sourceGUID == removed || transition.targetGUID == removed; });
                    }
                    items.erase(items.begin() + index); found = true;
                }
            });
            if (!found) throw std::runtime_error("Unknown graph collection");
            for (u32 state = 0; state < source.states.size(); ++state)
            {
                if (section != "states/" + std::to_string(state) + "/blendSamples") continue;
                auto& triangles = source.states[state].blendTriangles;
                std::erase_if(triangles, [index](const auto& triangle) { return std::find(triangle.samples.begin(), triangle.samples.end(), index) != triangle.samples.end(); });
                for (auto& triangle : triangles) for (auto& sample : triangle.samples) if (sample > index) --sample;
            }
            Edit(std::move(source)); return true;
        }
        catch (const std::exception& exception) { error = exception.what(); return false; }
    }

    std::vector<AnimationGraphField> AnimationGraphDocument::GetFields() const
    {
        std::vector<AnimationGraphField> fields;
        Authoring::Walk(_source, "", [&](const std::string& path, const auto& member)
        {
            using V = std::decay_t<decltype(member)>;
            if constexpr (Authoring::IsScalar<V> || Authoring::IsMath<V> || Authoring::IsArray<V>)
            {
                auto name = path.substr(1);
                const auto first = name.find('/');
                if (first == std::string::npos) { fields.push_back({"", 0, name, Authoring::Text(member)}); return; }
                const auto second = name.find('/', first + 1);
                if (second == std::string::npos) return;
                const auto section = name.substr(0, first);
                const u32 index = static_cast<u32>(std::stoul(name.substr(first + 1, second - first - 1)));
                name = name.substr(second + 1);
                // Component lists remain one editor control, including nested blend samples.
                const auto component = name.substr(name.find_last_of('/') + 1);
                if (component.find_first_not_of("0123456789") == std::string::npos) return;
                fields.push_back({section, index, name, Authoring::Text(member)});
            }
        });
        std::sort(fields.begin(), fields.end(), [](const auto& a, const auto& b) { return std::tie(a.section, a.index, a.name) < std::tie(b.section, b.index, b.name); });
        return fields;
    }

    std::vector<u8> AnimationGraphDocument::Serialize(const EditableAnimationGraph& source)
    {
        Authoring::BinaryWriter writer;
        Authoring::WriteHeader(writer, FileHeader::Type::AnimationGraph);
        writer.Value(source);
        return std::move(writer.bytes);
    }

    bool AnimationGraphDocument::Parse(std::span<const u8> bytes, EditableAnimationGraph& source, std::string& error)
    {
        error.clear();
        try
        {
            Authoring::BinaryReader reader(bytes);
            Authoring::ReadHeader(reader, FileHeader::Type::AnimationGraph);
            EditableAnimationGraph parsed;
            reader.Value(parsed);
            if (parsed.version != EditableAnimationGraph::CURRENT_VERSION || reader.Remaining()) throw std::runtime_error("Unsupported graph source version or trailing data");
            source = std::move(parsed); return true;
        }
        catch (const std::exception& exception) { error = exception.what(); return false; }
    }
}
