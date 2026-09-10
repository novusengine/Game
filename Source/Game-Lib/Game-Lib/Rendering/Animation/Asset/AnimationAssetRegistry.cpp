#include "AnimationAssetRegistry.h"
#include "AnimationAssetValidator.h"

#include "Game-Lib/Rendering/Asset/AssetValidation.h"

#include <Filesystem/PactStorage.h>

#include <limits>

namespace
{
    AssetLoading::Diagnostic MakeRegistryDiagnostic(AssetLoading::DiagnosticCode code, FileFormat::AssetID assetID, std::string_view field)
    {
        AssetLoading::Diagnostic diagnostic = {code, field};
        diagnostic.requestedAssetID = assetID;
        diagnostic.stage = code == AssetLoading::DiagnosticCode::MissingDependency ? AssetLoading::DiagnosticStage::ResolveDependency : AssetLoading::DiagnosticStage::Registry;
        return diagnostic;
    }

    template <typename Result>
    Result ReadRigFamily(std::span<const u8> payload)
    {
        Result result = AnimationLoading::AnimationAssetReader::ReadRigFamily(payload);
        return result && AssetLoading::ShouldPerformFullValidation() ? AnimationLoading::AnimationAssetValidator::Validate(std::move(result)) : result;
    }
    AnimationLoading::SkeletonAssetReadResult ReadSkeleton(std::span<const u8> payload)
    {
        AnimationLoading::SkeletonAssetReadResult result = AnimationLoading::AnimationAssetReader::ReadSkeleton(payload);
        return result && AssetLoading::ShouldPerformFullValidation() ? AnimationLoading::AnimationAssetValidator::Validate(std::move(result)) : result;
    }
    AnimationLoading::AnimationClipAssetReadResult ReadAnimationClip(std::span<const u8> payload)
    {
        AnimationLoading::AnimationClipAssetReadResult result = AnimationLoading::AnimationAssetReader::ReadAnimationClip(payload);
        return result && AssetLoading::ShouldPerformFullValidation() ? AnimationLoading::AnimationAssetValidator::Validate(std::move(result)) : result;
    }
    AnimationLoading::AnimationSetAssetReadResult ReadAnimationSet(std::span<const u8> payload)
    {
        AnimationLoading::AnimationSetAssetReadResult result = AnimationLoading::AnimationAssetReader::ReadAnimationSet(payload);
        return result && AssetLoading::ShouldPerformFullValidation() ? AnimationLoading::AnimationAssetValidator::Validate(std::move(result)) : result;
    }
    AnimationLoading::AnimationGraphAssetReadResult ReadAnimationGraph(std::span<const u8> payload) { return AnimationLoading::AnimationAssetReader::ReadAnimationGraph(payload); }
    AnimationLoading::BoneMaskAssetReadResult ReadBoneMask(std::span<const u8> payload)
    {
        AnimationLoading::BoneMaskAssetReadResult result = AnimationLoading::AnimationAssetReader::ReadBoneMask(payload);
        return result && AssetLoading::ShouldPerformFullValidation() ? AnimationLoading::AnimationAssetValidator::Validate(std::move(result)) : result;
    }
    AnimationLoading::IKRigAssetReadResult ReadIKRig(std::span<const u8> payload)
    {
        AnimationLoading::IKRigAssetReadResult result = AnimationLoading::AnimationAssetReader::ReadIKRig(payload);
        return result && AssetLoading::ShouldPerformFullValidation() ? AnimationLoading::AnimationAssetValidator::Validate(std::move(result)) : result;
    }
    AnimationLoading::RetargetProfileAssetReadResult ReadRetargetProfile(std::span<const u8> payload) { return AnimationLoading::AnimationAssetReader::ReadRetargetProfile(payload); }
    AnimationLoading::AnimationBoundsAssetReadResult ReadAnimationBounds(std::span<const u8> payload)
    {
        AnimationLoading::AnimationBoundsAssetReadResult result = AnimationLoading::AnimationAssetReader::ReadAnimationBounds(payload);
        return result && AssetLoading::ShouldPerformFullValidation() ? AnimationLoading::AnimationAssetValidator::Validate(std::move(result)) : result;
    }
} // namespace

namespace AnimationLoading
{
    template <typename Handle, typename View, typename ReadFunction>
    Handle AnimationAssetRegistry::LoadFromStorage(Table<Handle, View>& table, FileFormat::AssetID assetID, ReadFunction read)
    {
        const auto existing = table.assetToSlot.find(assetID);
        if (existing != table.assetToSlot.end())
        {
            typename Table<Handle, View>::Entry& entry = table.entries[existing->second];
            ++entry.referenceCount;
            ++_cacheHits;
            return Handle::Create(existing->second, entry.generation);
        }

        if (assetID == FileFormat::INVALID_ASSET_ID || _pactStorage == nullptr)
        {
            typename Table<Handle, View>::Entry entry;
            entry.assetID = assetID;
            entry.referenceCount = 1;
            entry.diagnostic = MakeRegistryDiagnostic(AssetLoading::DiagnosticCode::MissingDependency, assetID, "asset");
            const u32 slot = static_cast<u32>(table.entries.size());
            table.entries.push_back(std::move(entry));
            table.assetToSlot.emplace(assetID, slot);
            ++_failures;
            return Handle::Create(slot, 1);
        }

        PACT::PactFileHandle file;
        if (_pactStorage->ReadFile(assetID, file) != PACT::PactReadResult::Success)
        {
            typename Table<Handle, View>::Entry entry;
            entry.assetID = assetID;
            entry.referenceCount = 1;
            entry.diagnostic = MakeRegistryDiagnostic(AssetLoading::DiagnosticCode::MissingDependency, assetID, "asset");
            const u32 slot = static_cast<u32>(table.entries.size());
            table.entries.push_back(std::move(entry));
            table.assetToSlot.emplace(assetID, slot);
            ++_failures;
            return Handle::Create(slot, 1);
        }
        return Publish(table, assetID, {reinterpret_cast<const u8*>(file.GetData()), file.GetSize()}, read);
    }

    template <typename Handle, typename View, typename ReadFunction>
    Handle AnimationAssetRegistry::Publish(Table<Handle, View>& table, FileFormat::AssetID assetID, std::span<const u8> payload, ReadFunction read)
    {
        const auto ownedPayload = std::make_shared<const std::vector<u8>>(payload.begin(), payload.end());
        auto result = read(std::span<const u8>(*ownedPayload));
        if (result.diagnostic)
        {
            result.diagnostic.requestedAssetID = assetID;
            const auto existing = table.assetToSlot.find(assetID);
            if (existing != table.assetToSlot.end())
            {
                typename Table<Handle, View>::Entry& entry = table.entries[existing->second];
                if (!entry.record)
                    entry.diagnostic = result.diagnostic;
                ++_failures;
                return Handle::Create(existing->second, entry.generation);
            }
            typename Table<Handle, View>::Entry entry;
            entry.assetID = assetID;
            entry.referenceCount = 1;
            entry.diagnostic = result.diagnostic;
            const u32 slot = static_cast<u32>(table.entries.size());
            table.entries.push_back(std::move(entry));
            table.assetToSlot.emplace(assetID, slot);
            ++_failures;
            return Handle::Create(slot, 1);
        }

        auto record = std::make_shared<ImmutableRecord<View>>();
        record->payload = ownedPayload;
        record->view = result.view;
        const auto existing = table.assetToSlot.find(assetID);
        if (existing != table.assetToSlot.end())
        {
            typename Table<Handle, View>::Entry& entry = table.entries[existing->second];
            if (entry.referenceCount != 0)
            {
                entry.retiredGenerationReferences[entry.generation] += entry.referenceCount;
                if (entry.record)
                    entry.retiredRecords.emplace(entry.generation, entry.record);
            }
            ++entry.generation;
            if (entry.generation == 0)
                entry.generation = 1;
            entry.referenceCount = 1;
            entry.record = std::move(record);
            entry.diagnostic = {};
            ++_reloads;
            return Handle::Create(existing->second, entry.generation);
        }

        typename Table<Handle, View>::Entry entry;
        entry.assetID = assetID;
        entry.referenceCount = 1;
        entry.record = std::move(record);
        const u32 slot = static_cast<u32>(table.entries.size());
        table.entries.push_back(std::move(entry));
        table.assetToSlot.emplace(assetID, slot);
        return Handle::Create(slot, 1);
    }

    template <typename Handle, typename View>
    bool AnimationAssetRegistry::Release(Table<Handle, View>& table, Handle handle)
    {
        if (!handle || handle.GetSlot() >= table.entries.size())
            return false;
        typename Table<Handle, View>::Entry& entry = table.entries[handle.GetSlot()];
        if (entry.generation != handle.GetGeneration())
        {
            const auto retired = entry.retiredGenerationReferences.find(handle.GetGeneration());
            if (retired == entry.retiredGenerationReferences.end() || retired->second == 0)
                return false;
            --retired->second;
            if (retired->second == 0)
            {
                entry.retiredRecords.erase(handle.GetGeneration());
                entry.retiredGenerationReferences.erase(retired);
            }
            return true;
        }
        if (entry.referenceCount == 0)
            return false;
        --entry.referenceCount;
        return true;
    }

    template <typename Handle, typename View>
    const View* AnimationAssetRegistry::Get(const Table<Handle, View>& table, Handle handle, AssetLoading::Diagnostic* diagnostic) const
    {
        if (!handle || handle.GetSlot() >= table.entries.size())
        {
            if (diagnostic)
                *diagnostic = MakeRegistryDiagnostic(AssetLoading::DiagnosticCode::StaleHandle, FileFormat::INVALID_ASSET_ID, "handle");
            ++_staleHandleRejections;
            return nullptr;
        }
        const typename Table<Handle, View>::Entry& entry = table.entries[handle.GetSlot()];
        if (entry.generation != handle.GetGeneration())
        {
            if (diagnostic)
                *diagnostic = MakeRegistryDiagnostic(AssetLoading::DiagnosticCode::StaleHandle, entry.assetID, "handle.generation");
            ++_staleHandleRejections;
            return nullptr;
        }
        if (!entry.record)
        {
            if (diagnostic)
                *diagnostic = entry.diagnostic;
            return nullptr;
        }
        if (diagnostic)
            *diagnostic = {};
        return &entry.record->view;
    }

    template <typename Handle, typename View>
    const View* AnimationAssetRegistry::GetRetained(const Table<Handle, View>& table, Handle handle, AssetLoading::Diagnostic* diagnostic) const
    {
        if (handle && handle.GetSlot() < table.entries.size())
        {
            const auto& entry = table.entries[handle.GetSlot()];
            if (entry.generation != handle.GetGeneration())
            {
                const auto retired = entry.retiredRecords.find(handle.GetGeneration());
                if (retired != entry.retiredRecords.end())
                {
                    if (diagnostic) *diagnostic = {};
                    return &retired->second->view;
                }
            }
        }
        return Get(table, handle, diagnostic);
    }

    template <typename Handle, typename View>
    std::shared_ptr<const View> AnimationAssetRegistry::Lease(const Table<Handle, View>& table, Handle handle) const
    {
        if (!handle || handle.GetSlot() >= table.entries.size())
            return {};
        const auto& entry = table.entries[handle.GetSlot()];
        std::shared_ptr<const ImmutableRecord<View>> record;
        if (entry.generation == handle.GetGeneration())
            record = entry.record;
        else if (const auto retired = entry.retiredRecords.find(handle.GetGeneration()); retired != entry.retiredRecords.end())
            record = retired->second;
        return record ? std::shared_ptr<const View>(record, &record->view) : nullptr;
    }

    template <typename Handle, typename View>
    FileFormat::AssetID AnimationAssetRegistry::GetAssetID(const Table<Handle, View>& table, Handle handle) const
    {
        if (!handle || handle.GetSlot() >= table.entries.size())
            return FileFormat::INVALID_ASSET_ID;
        const typename Table<Handle, View>::Entry& entry = table.entries[handle.GetSlot()];
        return entry.generation == handle.GetGeneration() ? entry.assetID : FileFormat::INVALID_ASSET_ID;
    }

    template <typename Handle, typename View>
    void AnimationAssetRegistry::AccumulateStats(const Table<Handle, View>& table, u32 typeIndex, AnimationAssetRegistryStats& stats) const
    {
        stats.residentAssetsByType[typeIndex] = static_cast<u32>(table.entries.size());
        for (const typename Table<Handle, View>::Entry& entry : table.entries)
        {
            ++stats.residentAssets;
            stats.references += entry.referenceCount;
            for (const auto& retired : entry.retiredGenerationReferences)
                stats.references += retired.second;
            for (const auto& retired : entry.retiredRecords)
                stats.residentBytes += retired.second->payload->size();
            if (entry.record)
                stats.residentBytes += entry.record->payload->size();
            else
                ++stats.residentFailures;
        }
    }

#define DEFINE_ANIMATION_ASSET_REGISTRY(TypeName, FunctionName, MemberName, ReadName) \
    TypeName##Handle AnimationAssetRegistry::Load##FunctionName(FileFormat::AssetID assetID) { return LoadFromStorage(MemberName, assetID, ReadName); } \
    TypeName##Handle AnimationAssetRegistry::Publish##FunctionName(FileFormat::AssetID assetID, std::span<const u8> payload) { return Publish(MemberName, assetID, payload, ReadName); } \
    bool AnimationAssetRegistry::Release(TypeName##Handle handle) { return Release(MemberName, handle); } \
    std::shared_ptr<const TypeName##AssetView> AnimationAssetRegistry::Lease(TypeName##Handle handle) const { return Lease(MemberName, handle); } \
    const TypeName##AssetView* AnimationAssetRegistry::GetRetained(TypeName##Handle handle, AssetLoading::Diagnostic* diagnostic) const { return GetRetained(MemberName, handle, diagnostic); } \
    FileFormat::AssetID AnimationAssetRegistry::GetRetainedAssetID(TypeName##Handle handle) const { return handle && GetRetained(MemberName, handle, nullptr) ? MemberName.entries[handle.GetSlot()].assetID : FileFormat::INVALID_ASSET_ID; } \
    const TypeName##AssetView* AnimationAssetRegistry::Get(TypeName##Handle handle, AssetLoading::Diagnostic* diagnostic) const { return Get(MemberName, handle, diagnostic); } \
    FileFormat::AssetID AnimationAssetRegistry::GetAssetID(TypeName##Handle handle) const { return GetAssetID(MemberName, handle); }

    DEFINE_ANIMATION_ASSET_REGISTRY(RigFamily, RigFamily, _rigFamilies, ReadRigFamily<RigFamilyAssetReadResult>)
    DEFINE_ANIMATION_ASSET_REGISTRY(Skeleton, Skeleton, _skeletons, ReadSkeleton)
    DEFINE_ANIMATION_ASSET_REGISTRY(AnimationClip, AnimationClip, _animationClips, ReadAnimationClip)
    DEFINE_ANIMATION_ASSET_REGISTRY(AnimationSet, AnimationSet, _animationSets, ReadAnimationSet)
    DEFINE_ANIMATION_ASSET_REGISTRY(AnimationGraph, AnimationGraph, _animationGraphs, ReadAnimationGraph)
    DEFINE_ANIMATION_ASSET_REGISTRY(BoneMask, BoneMask, _boneMasks, ReadBoneMask)
    DEFINE_ANIMATION_ASSET_REGISTRY(IKRig, IKRig, _ikRigs, ReadIKRig)
    DEFINE_ANIMATION_ASSET_REGISTRY(RetargetProfile, RetargetProfile, _retargetProfiles, ReadRetargetProfile)
    DEFINE_ANIMATION_ASSET_REGISTRY(AnimationBounds, AnimationBounds, _animationBounds, ReadAnimationBounds)

#undef DEFINE_ANIMATION_ASSET_REGISTRY

    std::vector<std::string> AnimationAssetRegistry::GetFailureDescriptions() const
    {
        std::vector<std::string> failures;
        auto append = [&failures](const auto& table, const char* type)
        {
            for (const auto& entry : table.entries)
                if (!entry.record)
                    failures.push_back(std::string(type) + " " + std::to_string(entry.assetID) + ": " + AssetLoading::Describe(entry.diagnostic));
        };
        append(_rigFamilies, "RigFamily"); append(_skeletons, "Skeleton"); append(_animationClips, "Clip"); append(_animationSets, "Set"); append(_animationGraphs, "Graph"); append(_boneMasks, "Mask"); append(_ikRigs, "IKRig"); append(_retargetProfiles, "RetargetProfile"); append(_animationBounds, "Bounds");
        return failures;
    }

    AnimationAssetRegistryStats AnimationAssetRegistry::GetStats() const
    {
        AnimationAssetRegistryStats stats;
        AccumulateStats(_rigFamilies, 0, stats);
        AccumulateStats(_skeletons, 1, stats);
        AccumulateStats(_animationClips, 2, stats);
        AccumulateStats(_animationSets, 3, stats);
        AccumulateStats(_animationGraphs, 4, stats);
        AccumulateStats(_boneMasks, 5, stats);
        AccumulateStats(_ikRigs, 6, stats);
        AccumulateStats(_retargetProfiles, 7, stats);
        AccumulateStats(_animationBounds, 8, stats);
        for (const SkeletonTable::Entry& entry : _skeletons.entries)
            if (entry.record && entry.record->view.root.rigFamilyAssetID == FileFormat::INVALID_ASSET_ID)
                ++stats.skeletonsWithoutRigFamily;
        stats.cacheHits = _cacheHits;
        stats.failures = _failures;
        stats.reloads = _reloads;
        stats.staleHandleRejections = _staleHandleRejections;
        return stats;
    }
} // namespace AnimationLoading
