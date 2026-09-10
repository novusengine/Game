#include "AnimationAssetDocument.h"
#include "AnimationAssetValidator.h"
#include "AnimationAuthoring.h"

#include <Base/Memory/Bytebuffer.h>
#include <Base/Util/StringUtils.h>

#include <algorithm>
#include <cmath>
#include <variant>

namespace FileFormat::Animation
{
    template <typename T, typename F> requires std::is_same_v<std::remove_const_t<T>, LocalTransform>
    void VisitMembers(T& value, F&& field) { field("translation", value.translation); field("rotation", value.rotation); field("scale", value.scale); }
    template <typename T, typename F> requires std::is_same_v<std::remove_const_t<T>, RigFamilyJoint>
    void VisitMembers(T& value, F&& field) { field("semanticID", value.semanticID); field("parentJointIndex", value.parentJointIndex); field("flags", value.flags); field("canonicalTransform", value.canonicalTransform); }
    template <typename T, typename F> requires std::is_same_v<std::remove_const_t<T>, RigFamilyChain>
    void VisitMembers(T& value, F&& field) { field("semanticID", value.semanticID); field("jointIndexOffset", value.jointIndexOffset); field("numJointIndices", value.numJointIndices); field("flags", value.flags); }
    template <typename T, typename F> requires std::is_same_v<std::remove_const_t<T>, RigFamilyAsset>
    void VisitMembers(T& value, F&& field) { field("flags", value.flags); }
    template <typename T, typename F> requires std::is_same_v<std::remove_const_t<T>, RigFamilyData>
    void VisitMembers(T& value, F&& field) { field("joints", value.joints); field("chains", value.chains); field("chainJointIndices", value.chainJointIndices); }
    template <typename T, typename F> requires std::is_same_v<std::remove_const_t<T>, SkeletonJoint>
    void VisitMembers(T& value, F&& field) { field("semanticID", value.semanticID); field("parentJointIndex", value.parentJointIndex); field("familyJointIndex", value.familyJointIndex); field("hierarchyDepth", value.hierarchyDepth); field("flags", value.flags); field("restTransform", value.restTransform); field("inverseBindTransform", value.inverseBindTransform); }
    template <typename T, typename F> requires std::is_same_v<std::remove_const_t<T>, SkeletonFamilyBinding>
    void VisitMembers(T& value, F&& field) { field("skeletonJointIndex", value.skeletonJointIndex); field("familyJointIndex", value.familyJointIndex); field("skeletonToFamily", value.skeletonToFamily); field("familyToSkeleton", value.familyToSkeleton); field("familyReferenceRotation", value.familyReferenceRotation); }
    template <typename T, typename F> requires std::is_same_v<std::remove_const_t<T>, SkeletonPropagationRule>
    void VisitMembers(T& value, F&& field) { field("sourceJointIndex", value.sourceJointIndex); field("targetJointIndex", value.targetJointIndex); field("type", value.type); field("flags", value.flags); field("executionOrder", value.executionOrder); field("weight", value.weight); field("offsetTransform", value.offsetTransform); }
    template <typename T, typename F> requires std::is_same_v<std::remove_const_t<T>, HierarchyDepthRange>
    void VisitMembers(T& value, F&& field) { field("jointIndexOffset", value.jointIndexOffset); field("numJointIndices", value.numJointIndices); }
    template <typename T, typename F> requires std::is_same_v<std::remove_const_t<T>, SkeletonAttachment>
    void VisitMembers(T& value, F&& field) { field("semanticID", value.semanticID); field("jointIndex", value.jointIndex); field("flags", value.flags); field("localTransform", value.localTransform); }
    template <typename T, typename F> requires std::is_same_v<std::remove_const_t<T>, SkeletonAsset>
    void VisitMembers(T& value, F&& field) { field("rigFamilyAssetID", value.rigFamilyAssetID); field("flags", value.flags); }
    template <typename T, typename F> requires std::is_same_v<std::remove_const_t<T>, SkeletonData>
    void VisitMembers(T& value, F&& field) { field("joints", value.joints); field("familyBindings", value.familyBindings); field("propagationRules", value.propagationRules); field("hierarchyDepthRanges", value.hierarchyDepthRanges); field("hierarchyDepthJointIndices", value.hierarchyDepthJointIndices); field("attachments", value.attachments); }
    template <typename T, typename F> requires std::is_same_v<std::remove_const_t<T>, AnimationSetSlot>
    void VisitMembers(T& value, F&& field) { field("semanticID", value.semanticID); field("variantOffset", value.variantOffset); field("numVariants", value.numVariants); field("fallbackSlotIndex", value.fallbackSlotIndex); field("flags", value.flags); }
    template <typename T, typename F> requires std::is_same_v<std::remove_const_t<T>, AnimationClipVariant>
    void VisitMembers(T& value, F&& field) { field("animationClipAssetID", value.animationClipAssetID); field("selectionWeight", value.selectionWeight); field("flags", value.flags); field("stableVariationID", value.stableVariationID); }
    template <typename T, typename F> requires std::is_same_v<std::remove_const_t<T>, AnimationSetAsset>
    void VisitMembers(T& value, F&& field) { field("declaredPoseDomainAssetID", value.declaredPoseDomainAssetID); field("declaredPoseDomainType", value.declaredPoseDomainType); field("flags", value.flags); }
    template <typename T, typename F> requires std::is_same_v<std::remove_const_t<T>, AnimationSetData>
    void VisitMembers(T& value, F&& field) { field("slots", value.slots); field("variants", value.variants); }
    template <typename T, typename F> requires std::is_same_v<std::remove_const_t<T>, BoneMaskAsset>
    void VisitMembers(T& value, F&& field) { field("poseDomainAssetID", value.poseDomainAssetID); field("poseDomainType", value.poseDomainType); field("flags", value.flags); }
    template <typename T, typename F> requires std::is_same_v<std::remove_const_t<T>, BoneMaskData>
    void VisitMembers(T& value, F&& field) { field("jointWeights", value.jointWeights); }
    template <typename T, typename F> requires std::is_same_v<std::remove_const_t<T>, IKJointLimit>
    void VisitMembers(T& value, F&& field) { field("minimumRadians", value.minimumRadians); field("maximumRadians", value.maximumRadians); }
    template <typename T, typename F> requires std::is_same_v<std::remove_const_t<T>, IKChain>
    void VisitMembers(T& value, F&& field) { field("semanticID", value.semanticID); field("jointIndexOffset", value.jointIndexOffset); field("numJointIndices", value.numJointIndices); field("jointLimitOffset", value.jointLimitOffset); field("numJointLimits", value.numJointLimits); field("effectorJointIndex", value.effectorJointIndex); field("poleJointIndex", value.poleJointIndex); field("preferredBendDirection", value.preferredBendDirection); field("maximumReach", value.maximumReach); }
    template <typename T, typename F> requires std::is_same_v<std::remove_const_t<T>, IKSolver>
    void VisitMembers(T& value, F&& field) { field("semanticID", value.semanticID); field("chainIndex", value.chainIndex); field("type", value.type); field("goalSpace", value.goalSpace); field("minimumQualityTier", value.minimumQualityTier); field("flags", value.flags); field("maximumIterations", value.maximumIterations); field("convergenceTolerance", value.convergenceTolerance); field("maximumReachScale", value.maximumReachScale); }
    template <typename T, typename F> requires std::is_same_v<std::remove_const_t<T>, IKRigAsset>
    void VisitMembers(T& value, F&& field) { field("skeletonAssetID", value.skeletonAssetID); field("conservativeExtentExpansion", value.conservativeExtentExpansion); field("maximumGoalDistance", value.maximumGoalDistance); field("flags", value.flags); }
    template <typename T, typename F> requires std::is_same_v<std::remove_const_t<T>, IKRigData>
    void VisitMembers(T& value, F&& field) { field("chains", value.chains); field("chainJointIndices", value.chainJointIndices); field("jointLimits", value.jointLimits); field("solvers", value.solvers); }
    template <typename T, typename F> requires std::is_same_v<std::remove_const_t<T>, SynchronizationMarker>
    void VisitMembers(T& value, F&& field) { field("semanticID", value.semanticID); field("timeMicroseconds", value.timeMicroseconds); field("flags", value.flags); }
    template <typename T, typename F> requires std::is_same_v<std::remove_const_t<T>, ActionWindow>
    void VisitMembers(T& value, F&& field) { field("semanticID", value.semanticID); field("startTimeMicroseconds", value.startTimeMicroseconds); field("endTimeMicroseconds", value.endTimeMicroseconds); }
    template <typename T, typename F> requires std::is_same_v<std::remove_const_t<T>, AnimationEvent>
    void VisitMembers(T& value, F&& field) { field("timeMicroseconds", value.timeMicroseconds); field("eventTypeID", value.eventTypeID); field("payloadTypeID", value.payloadTypeID); field("flags", value.flags); field("payloadOffset", value.payloadOffset); field("payloadSize", value.payloadSize); }
    template <typename T, typename F> requires std::is_same_v<std::remove_const_t<T>, AnimationClipAsset>
    void VisitMembers(T& value, F&& field) { field("sourcePoseDomainAssetID", value.sourcePoseDomainAssetID); field("sourcePoseDomainType", value.sourcePoseDomainType); field("durationMicroseconds", value.durationMicroseconds); field("sampleRateHz", value.sampleRateHz); field("sampleCount", value.sampleCount); field("flags", value.flags); field("additiveReferenceClipAssetID", value.additiveReferenceClipAssetID); field("additiveReferenceTimeMicroseconds", value.additiveReferenceTimeMicroseconds); field("additiveReferenceType", value.additiveReferenceType); }
}

namespace Animation
{
    namespace FA = FileFormat::Animation;
    struct ClipMetadata
    {
        std::vector<FA::SynchronizationMarker> synchronizationMarkers;
        std::vector<FA::ActionWindow> actionWindows;
        std::vector<FA::AnimationEvent> events;
        std::vector<u8> eventPayloadBytes;
    };
    template <typename T, typename F> requires std::is_same_v<std::remove_const_t<T>, ClipMetadata>
    void VisitMembers(T& value, F&& field)
    {
        field("synchronizationMarkers", value.synchronizationMarkers); field("actionWindows", value.actionWindows); field("events", value.events); field("eventPayloadBytes", value.eventPayloadBytes);
    }
    template <typename Root, typename Data> struct AssetDraft { Root asset; Data data; };
    template <typename Root, typename Data, typename F> void VisitMembers(AssetDraft<Root, Data>& value, F&& field) { field("asset", value.asset); field("data", value.data); }
    template <typename Root, typename Data, typename F> void VisitMembers(const AssetDraft<Root, Data>& value, F&& field) { field("asset", value.asset); field("data", value.data); }
    using Draft = std::variant<AssetDraft<FA::SkeletonAsset, FA::SkeletonData>, AssetDraft<FA::RigFamilyAsset, FA::RigFamilyData>, AssetDraft<FA::AnimationClipAsset, ClipMetadata>, AssetDraft<FA::AnimationSetAsset, FA::AnimationSetData>, AssetDraft<FA::BoneMaskAsset, FA::BoneMaskData>, AssetDraft<FA::IKRigAsset, FA::IKRigData>>;
    static FileHeader::Type SourceType(AnimationDocumentType type)
    {
        constexpr FileHeader::Type types[] = {FileHeader::Type::Skeleton, FileHeader::Type::RigFamily, FileHeader::Type::AnimationClip, FileHeader::Type::AnimationSet, FileHeader::Type::BoneMask, FileHeader::Type::IKRig};
        if (static_cast<size_t>(type) >= std::size(types)) throw std::runtime_error("Unknown animation document type");
        return types[static_cast<size_t>(type)];
    }
    struct AnimationAssetDocument::Impl
    {
        AnimationDocumentType type = AnimationDocumentType::Mask;
        Draft source = AssetDraft<FA::BoneMaskAsset, FA::BoneMaskData>{};
        Draft saved = source;
        struct Revision { Draft source; u64 id; };
        std::deque<Revision> undo;
        std::deque<Revision> redo;
        std::vector<u8> compiled;
        FA::AnimationClipData samples;
        u64 sampleContentHash = 0;
        u64 revision = 0;
        u64 nextRevision = 2;
        u64 savedRevision = 1;

        void Change(Draft next)
        {
            if (undo.size() == 32) undo.pop_front();
            undo.push_back({source, revision}); redo.clear(); source = std::move(next); revision = nextRevision++;
        }
    };

    template <typename Root, typename Data> static std::vector<u8> Cook(Root root, const Data& data)
    {
        auto buffer = Bytebuffer::BorrowRuntime(root.GetSerializedSize(data));
        if (!root.Save(buffer, data)) throw std::runtime_error("Asset serialization failed");
        return {buffer->GetDataPointer(), buffer->GetDataPointer() + buffer->writtenData};
    }

    AnimationAssetDocument::AnimationAssetDocument() : _impl(std::make_unique<Impl>()) {}
    AnimationAssetDocument::~AnimationAssetDocument() = default;

    bool AnimationAssetDocument::Open(AnimationDocumentType type, std::span<const u8> payload, std::string& error)
    {
        error.clear();
        auto next = std::make_unique<Impl>();
        next->type = type;
        const auto Read = [&](auto result, auto data)
        {
            if (!result) { error = AssetLoading::Describe(result.diagnostic); return false; }
            auto values = data(result.view);
            next->source = AssetDraft<std::decay_t<decltype(result.view.root)>, decltype(values)>{result.view.root, std::move(values)};
            return true;
        };
        using R = AnimationLoading::AnimationAssetReader;
        using V = AnimationLoading::AnimationAssetValidator;
        bool valid = false;
        switch (type)
        {
            case AnimationDocumentType::Skeleton:
                valid = Read(V::Validate(R::ReadSkeleton(payload)), [](const auto& v) { return FA::SkeletonData{{v.joints.begin(), v.joints.end()}, {v.familyBindings.begin(), v.familyBindings.end()}, {v.propagationRules.begin(), v.propagationRules.end()}, {v.hierarchyDepthRanges.begin(), v.hierarchyDepthRanges.end()}, {v.hierarchyDepthJointIndices.begin(), v.hierarchyDepthJointIndices.end()}, {v.attachments.begin(), v.attachments.end()}}; }); break;
            case AnimationDocumentType::RigFamily:
                valid = Read(V::Validate(R::ReadRigFamily(payload)), [](const auto& v) { return FA::RigFamilyData{{v.joints.begin(), v.joints.end()}, {v.chains.begin(), v.chains.end()}, {v.chainJointIndices.begin(), v.chainJointIndices.end()}}; }); break;
            case AnimationDocumentType::Set:
                valid = Read(V::Validate(R::ReadAnimationSet(payload)), [](const auto& v) { return FA::AnimationSetData{{v.slots.begin(), v.slots.end()}, {v.variants.begin(), v.variants.end()}}; }); break;
            case AnimationDocumentType::Mask:
                valid = Read(V::Validate(R::ReadBoneMask(payload)), [](const auto& v) { return FA::BoneMaskData{{v.jointWeights.begin(), v.jointWeights.end()}}; }); break;
            case AnimationDocumentType::IK:
                valid = Read(V::Validate(R::ReadIKRig(payload)), [](const auto& v) { return FA::IKRigData{{v.chains.begin(), v.chains.end()}, {v.chainJointIndices.begin(), v.chainJointIndices.end()}, {v.jointLimits.begin(), v.jointLimits.end()}, {v.solvers.begin(), v.solvers.end()}}; }); break;
            case AnimationDocumentType::Clip:
                valid = Read(V::Validate(R::ReadAnimationClip(payload)), [&](const auto& v)
                {
                    next->samples = {{v.tracks.begin(), v.tracks.end()}, {v.translationSamples.begin(), v.translationSamples.end()}, {v.rotationSamples.begin(), v.rotationSamples.end()}, {v.scaleSamples.begin(), v.scaleSamples.end()}};
                    return ClipMetadata{{v.synchronizationMarkers.begin(), v.synchronizationMarkers.end()}, {v.actionWindows.begin(), v.actionWindows.end()}, {v.events.begin(), v.events.end()}, {v.eventPayloadBytes.begin(), v.eventPayloadBytes.end()}};
                }); break;
        }
        if (!valid) return false;
        if (type == AnimationDocumentType::Clip)
        {
            u64 hash = 0;
            auto add = [&hash](const auto& values) { hash = XXHash64::hash(values.data(), values.size() * sizeof(typename std::decay_t<decltype(values)>::value_type), hash); };
            add(next->samples.tracks); add(next->samples.translationSamples); add(next->samples.rotationSamples); add(next->samples.scaleSamples);
            next->sampleContentHash = hash;
        }
        next->saved = next->source;
        next->compiled.assign(payload.begin(), payload.end());
        next->revision = 1;
        _impl = std::move(next);
        return true;
    }

    bool AnimationAssetDocument::Compile(std::string& error)
    {
        error.clear();
        try
        {
            auto bytes = std::visit([&](const auto& draft)
            {
                if constexpr (std::is_same_v<std::decay_t<decltype(draft.data)>, ClipMetadata>)
                {
                    auto clip = _impl->samples;
                    clip.synchronizationMarkers = draft.data.synchronizationMarkers;
                    clip.actionWindows = draft.data.actionWindows;
                    clip.events = draft.data.events;
                    clip.eventPayloadBytes = draft.data.eventPayloadBytes;
                    return Cook(draft.asset, clip);
                }
                else return Cook(draft.asset, draft.data);
            }, _impl->source);
            AnimationAssetDocument validation;
            if (!validation.Open(_impl->type, bytes, error)) return false;
            _impl->compiled = std::move(bytes);
            return true;
        }
        catch (const std::exception& exception) { error = exception.what(); return false; }
    }

    bool AnimationAssetDocument::Edit(std::string_view path, std::string_view value, std::string& error)
    {
        error.clear();
        try
        {
            if (!path.starts_with("/asset/") && !path.starts_with("/data/")) throw std::runtime_error("Choose an asset or data field");
            auto next = _impl->source;
            std::visit([&](auto& draft) { Authoring::EditField(draft, std::string(path), value); }, next);
            _impl->Change(std::move(next));
            return true;
        }
        catch (const std::exception& exception) { error = exception.what(); return false; }
    }

    bool AnimationAssetDocument::Insert(std::string_view path, std::string& error)
    {
        error.clear();
        try
        {
            auto next = _impl->source;
            bool found = false;
            std::visit([&](auto& draft)
            {
                Authoring::Walk(draft, "", [&](const std::string& candidate, auto& values)
                {
                    using V = std::decay_t<decltype(values)>;
                    if constexpr (Authoring::IsVector<V>)
                    {
                        if (candidate != path) return;
                        if (values.size() >= 4096) throw std::runtime_error("Collection has reached its limit");
                        values.emplace_back(); found = true;
                    }
                });
            }, next);
            if (!found) throw std::runtime_error("Unknown asset collection");
            _impl->Change(std::move(next));
            return true;
        }
        catch (const std::exception& exception) { error = exception.what(); return false; }
    }

    bool AnimationAssetDocument::Remove(std::string_view path, u32 index, std::string& error)
    {
        error.clear();
        try
        {
            auto next = _impl->source;
            bool found = false;
            std::visit([&](auto& draft)
            {
                Authoring::Walk(draft, "", [&](const std::string& candidate, auto& values)
                {
                    using V = std::decay_t<decltype(values)>;
                    if constexpr (Authoring::IsVector<V>)
                    {
                        if (candidate != path) return;
                        if (index >= values.size()) throw std::runtime_error("Collection index is out of range");
                        values.erase(values.begin() + index); found = true;
                    }
                });
            }, next);
            if (!found) throw std::runtime_error("Unknown asset collection");
            _impl->Change(std::move(next));
            return true;
        }
        catch (const std::exception& exception) { error = exception.what(); return false; }
    }

    bool AnimationAssetDocument::PaintMask(std::span<const FA::SkeletonJoint> joints, u32 joint, f32 weight, f32 falloff, bool subtree, std::string& error)
    {
        error.clear();
        if (_impl->type != AnimationDocumentType::Mask || joint >= joints.size() || !std::isfinite(weight) || !std::isfinite(falloff) || weight < 0 || weight > 1 || falloff < 0 || falloff > 1) { error = "Invalid mask brush"; return false; }
        auto next = _impl->source;
        auto& weights = std::get<AssetDraft<FA::BoneMaskAsset, FA::BoneMaskData>>(next).data.jointWeights;
        if (weights.size() != joints.size()) { error = "Mask and hierarchy joint counts differ"; return false; }
        for (u32 i = 0; i < joints.size(); ++i)
        {
            i32 ancestor = static_cast<i32>(i);
            u32 depth = 0;
            while (ancestor >= 0 && static_cast<u32>(ancestor) != joint && depth <= joints.size())
            {
                if (static_cast<u32>(ancestor) >= joints.size()) { error = "Invalid mask hierarchy"; return false; }
                ancestor = joints[ancestor].parentJointIndex;
                ++depth;
            }
            if (ancestor == static_cast<i32>(joint) && (subtree || i == joint))
                weights[i] = static_cast<u8>(std::lround(255.0f * weight * std::pow(falloff, static_cast<f32>(depth))));
        }
        _impl->Change(std::move(next));
        return true;
    }

    bool AnimationAssetDocument::MirrorMask(std::span<const std::pair<u32, u32>> pairs, std::string& error)
    {
        error.clear();
        if (_impl->type != AnimationDocumentType::Mask) { error = "Not a mask document"; return false; }
        auto next = _impl->source;
        auto& weights = std::get<AssetDraft<FA::BoneMaskAsset, FA::BoneMaskData>>(next).data.jointWeights;
        for (auto [source, target] : pairs)
        {
            if (source >= weights.size() || target >= weights.size()) { error = "Mirror pair is out of range"; return false; }
            weights[target] = std::get<AssetDraft<FA::BoneMaskAsset, FA::BoneMaskData>>(_impl->source).data.jointWeights[source];
        }
        _impl->Change(std::move(next));
        return true;
    }

    bool AnimationAssetDocument::Undo() { if (_impl->undo.empty()) return false; _impl->redo.push_back({_impl->source, _impl->revision}); _impl->source = std::move(_impl->undo.back().source); _impl->revision = _impl->undo.back().id; _impl->undo.pop_back(); return true; }
    bool AnimationAssetDocument::Redo() { if (_impl->redo.empty()) return false; _impl->undo.push_back({_impl->source, _impl->revision}); _impl->source = std::move(_impl->redo.back().source); _impl->revision = _impl->redo.back().id; _impl->redo.pop_back(); return true; }
    void AnimationAssetDocument::MarkSaved() { _impl->saved = _impl->source; _impl->savedRevision = _impl->revision; }
    void AnimationAssetDocument::Revert() { _impl->Change(_impl->saved); _impl->revision = _impl->savedRevision; }
    std::vector<u8> AnimationAssetDocument::Serialize() const
    {
        Authoring::BinaryWriter writer;
        Authoring::WriteHeader(writer, SourceType(_impl->type));
        writer.Value(_impl->sampleContentHash);
        std::visit([&](const auto& draft) { writer.Value(draft); }, _impl->source);
        return std::move(writer.bytes);
    }
    bool AnimationAssetDocument::LoadSource(std::span<const u8> bytes, std::string& error)
    {
        error.clear();
        try
        {
            Authoring::BinaryReader reader(bytes);
            Authoring::ReadHeader(reader, SourceType(_impl->type));
            u64 sampleHash; reader.Value(sampleHash);
            if (sampleHash != _impl->sampleContentHash) throw std::runtime_error("Clip source belongs to different sample streams; open its original clip first");
            auto source = _impl->source;
            std::visit([&](auto& draft) { reader.Value(draft); }, source);
            if (reader.Remaining()) throw std::runtime_error("Trailing data in animation source");
            _impl->Change(std::move(source)); return true;
        }
        catch (const std::exception& exception) { error = exception.what(); return false; }
    }
    std::vector<AnimationDocumentField> AnimationAssetDocument::GetFields() const
    {
        std::vector<AnimationDocumentField> fields;
        std::visit([&](const auto& draft)
        {
            Authoring::Walk(draft, "", [&](const std::string& path, const auto& member)
            {
                if constexpr (Authoring::IsScalar<std::decay_t<decltype(member)>>) fields.push_back({path, Authoring::Text(member)});
            });
        }, _impl->source);
        std::sort(fields.begin(), fields.end(), [](const auto& a, const auto& b) { return a.path < b.path; });
        return fields;
    }
    const std::vector<u8>& AnimationAssetDocument::GetPayload() const { return _impl->compiled; }
    AnimationDocumentType AnimationAssetDocument::GetType() const { return _impl->type; }
    u64 AnimationAssetDocument::GetRevision() const { return _impl->revision; }
    bool AnimationAssetDocument::IsDirty() const
    {
        return _impl->revision != _impl->savedRevision;
    }
}
