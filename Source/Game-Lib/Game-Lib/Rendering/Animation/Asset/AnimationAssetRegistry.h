#pragma once
#include "AnimationAssetReader.h"

#include <robinhood/robinhood.h>

#include <array>
#include <memory>
#include <vector>

namespace PACT
{
    class PactStorage;
}

namespace AnimationLoading
{
    template <typename Tag>
    class AnimationAssetHandle
    {
      public:
        constexpr AnimationAssetHandle() = default;
        explicit constexpr operator bool() const { return _value != 0; }
        constexpr bool operator==(const AnimationAssetHandle&) const = default;
        constexpr u64 GetRawValue() const { return _value; }

      private:
        friend class AnimationAssetRegistry;
        explicit constexpr AnimationAssetHandle(u64 value) : _value(value) {}
        constexpr u32 GetSlot() const { return static_cast<u32>(_value) - 1u; }
        constexpr u32 GetGeneration() const { return static_cast<u32>(_value >> 32u); }
        static constexpr AnimationAssetHandle Create(u32 slot, u32 generation) { return AnimationAssetHandle((static_cast<u64>(generation) << 32u) | (static_cast<u64>(slot) + 1u)); }
        u64 _value = 0;
    };

    struct RigFamilyAssetTag;
    struct SkeletonAssetTag;
    struct AnimationClipAssetTag;
    struct AnimationSetAssetTag;
    struct AnimationGraphAssetTag;
    struct BoneMaskAssetTag;
    struct IKRigAssetTag;
    struct RetargetProfileAssetTag;
    struct AnimationBoundsAssetTag;

    using RigFamilyHandle = AnimationAssetHandle<RigFamilyAssetTag>;
    using SkeletonHandle = AnimationAssetHandle<SkeletonAssetTag>;
    using AnimationClipHandle = AnimationAssetHandle<AnimationClipAssetTag>;
    using AnimationSetHandle = AnimationAssetHandle<AnimationSetAssetTag>;
    using AnimationGraphHandle = AnimationAssetHandle<AnimationGraphAssetTag>;
    using BoneMaskHandle = AnimationAssetHandle<BoneMaskAssetTag>;
    using IKRigHandle = AnimationAssetHandle<IKRigAssetTag>;
    using RetargetProfileHandle = AnimationAssetHandle<RetargetProfileAssetTag>;
    using AnimationBoundsHandle = AnimationAssetHandle<AnimationBoundsAssetTag>;

    struct AnimationAssetRegistryStats
    {
        u32 residentAssets = 0;
        u32 residentFailures = 0;
        u32 references = 0;
        u32 cacheHits = 0;
        u32 failures = 0;
        u32 reloads = 0;
        u32 staleHandleRejections = 0;
        u32 skeletonsWithoutRigFamily = 0;
        u64 residentBytes = 0;
        std::array<u32, 9> residentAssetsByType = {};
    };

    class AnimationAssetRegistry
    {
      public:
        explicit AnimationAssetRegistry(PACT::PactStorage* pactStorage = nullptr) : _pactStorage(pactStorage) {}

        RigFamilyHandle LoadRigFamily(FileFormat::AssetID assetID);
        SkeletonHandle LoadSkeleton(FileFormat::AssetID assetID);
        AnimationClipHandle LoadAnimationClip(FileFormat::AssetID assetID);
        AnimationSetHandle LoadAnimationSet(FileFormat::AssetID assetID);
        AnimationGraphHandle LoadAnimationGraph(FileFormat::AssetID assetID);
        BoneMaskHandle LoadBoneMask(FileFormat::AssetID assetID);
        IKRigHandle LoadIKRig(FileFormat::AssetID assetID);
        RetargetProfileHandle LoadRetargetProfile(FileFormat::AssetID assetID);
        AnimationBoundsHandle LoadAnimationBounds(FileFormat::AssetID assetID);

        RigFamilyHandle PublishRigFamily(FileFormat::AssetID assetID, std::span<const u8> payload);
        SkeletonHandle PublishSkeleton(FileFormat::AssetID assetID, std::span<const u8> payload);
        AnimationClipHandle PublishAnimationClip(FileFormat::AssetID assetID, std::span<const u8> payload);
        AnimationSetHandle PublishAnimationSet(FileFormat::AssetID assetID, std::span<const u8> payload);
        AnimationGraphHandle PublishAnimationGraph(FileFormat::AssetID assetID, std::span<const u8> payload);
        BoneMaskHandle PublishBoneMask(FileFormat::AssetID assetID, std::span<const u8> payload);
        IKRigHandle PublishIKRig(FileFormat::AssetID assetID, std::span<const u8> payload);
        RetargetProfileHandle PublishRetargetProfile(FileFormat::AssetID assetID, std::span<const u8> payload);
        AnimationBoundsHandle PublishAnimationBounds(FileFormat::AssetID assetID, std::span<const u8> payload);

        bool Release(RigFamilyHandle handle);
        bool Release(SkeletonHandle handle);
        bool Release(AnimationClipHandle handle);
        bool Release(AnimationSetHandle handle);
        bool Release(AnimationGraphHandle handle);
        bool Release(BoneMaskHandle handle);
        bool Release(IKRigHandle handle);
        bool Release(RetargetProfileHandle handle);
        bool Release(AnimationBoundsHandle handle);

        const RigFamilyAssetView* Get(RigFamilyHandle handle, AssetLoading::Diagnostic* diagnostic = nullptr) const;
        const SkeletonAssetView* Get(SkeletonHandle handle, AssetLoading::Diagnostic* diagnostic = nullptr) const;
        const AnimationClipAssetView* Get(AnimationClipHandle handle, AssetLoading::Diagnostic* diagnostic = nullptr) const;
        const AnimationSetAssetView* Get(AnimationSetHandle handle, AssetLoading::Diagnostic* diagnostic = nullptr) const;
        const AnimationGraphAssetView* Get(AnimationGraphHandle handle, AssetLoading::Diagnostic* diagnostic = nullptr) const;
        const BoneMaskAssetView* Get(BoneMaskHandle handle, AssetLoading::Diagnostic* diagnostic = nullptr) const;
        const IKRigAssetView* Get(IKRigHandle handle, AssetLoading::Diagnostic* diagnostic = nullptr) const;
        const RetargetProfileAssetView* Get(RetargetProfileHandle handle, AssetLoading::Diagnostic* diagnostic = nullptr) const;
        const AnimationBoundsAssetView* Get(AnimationBoundsHandle handle, AssetLoading::Diagnostic* diagnostic = nullptr) const;

        FileFormat::AssetID GetAssetID(RigFamilyHandle handle) const;
        FileFormat::AssetID GetAssetID(SkeletonHandle handle) const;
        FileFormat::AssetID GetAssetID(AnimationClipHandle handle) const;
        FileFormat::AssetID GetAssetID(AnimationSetHandle handle) const;
        FileFormat::AssetID GetAssetID(AnimationGraphHandle handle) const;
        FileFormat::AssetID GetAssetID(BoneMaskHandle handle) const;
        FileFormat::AssetID GetAssetID(IKRigHandle handle) const;
        FileFormat::AssetID GetAssetID(RetargetProfileHandle handle) const;
        FileFormat::AssetID GetAssetID(AnimationBoundsHandle handle) const;

        AnimationAssetRegistryStats GetStats() const;

      private:
        template <typename View>
        struct ImmutableRecord
        {
            std::shared_ptr<const std::vector<u8>> payload;
            View view;
        };

        template <typename Handle, typename View>
        struct Table
        {
            struct Entry
            {
                FileFormat::AssetID assetID = FileFormat::INVALID_ASSET_ID;
                u32 generation = 1;
                u32 referenceCount = 0;
                robin_hood::unordered_map<u32, u32> retiredGenerationReferences;
                std::shared_ptr<const ImmutableRecord<View>> record;
                AssetLoading::Diagnostic diagnostic;
            };
            std::vector<Entry> entries;
            robin_hood::unordered_map<FileFormat::AssetID, u32> assetToSlot;
        };

        using RigFamilyTable = Table<RigFamilyHandle, RigFamilyAssetView>;
        using SkeletonTable = Table<SkeletonHandle, SkeletonAssetView>;
        using AnimationClipTable = Table<AnimationClipHandle, AnimationClipAssetView>;
        using AnimationSetTable = Table<AnimationSetHandle, AnimationSetAssetView>;
        using AnimationGraphTable = Table<AnimationGraphHandle, AnimationGraphAssetView>;
        using BoneMaskTable = Table<BoneMaskHandle, BoneMaskAssetView>;
        using IKRigTable = Table<IKRigHandle, IKRigAssetView>;
        using RetargetProfileTable = Table<RetargetProfileHandle, RetargetProfileAssetView>;
        using AnimationBoundsTable = Table<AnimationBoundsHandle, AnimationBoundsAssetView>;

        template <typename Handle, typename View, typename ReadFunction>
        Handle LoadFromStorage(Table<Handle, View>& table, FileFormat::AssetID assetID, ReadFunction read);
        template <typename Handle, typename View, typename ReadFunction>
        Handle Publish(Table<Handle, View>& table, FileFormat::AssetID assetID, std::span<const u8> payload, ReadFunction read);
        template <typename Handle, typename View>
        bool Release(Table<Handle, View>& table, Handle handle);
        template <typename Handle, typename View>
        const View* Get(const Table<Handle, View>& table, Handle handle, AssetLoading::Diagnostic* diagnostic) const;
        template <typename Handle, typename View>
        FileFormat::AssetID GetAssetID(const Table<Handle, View>& table, Handle handle) const;
        template <typename Handle, typename View>
        void AccumulateStats(const Table<Handle, View>& table, u32 typeIndex, AnimationAssetRegistryStats& stats) const;

        PACT::PactStorage* _pactStorage = nullptr;
        RigFamilyTable _rigFamilies;
        SkeletonTable _skeletons;
        AnimationClipTable _animationClips;
        AnimationSetTable _animationSets;
        AnimationGraphTable _animationGraphs;
        BoneMaskTable _boneMasks;
        IKRigTable _ikRigs;
        RetargetProfileTable _retargetProfiles;
        AnimationBoundsTable _animationBounds;
        u32 _cacheHits = 0;
        u32 _failures = 0;
        u32 _reloads = 0;
        mutable u32 _staleHandleRejections = 0;
    };
} // namespace AnimationLoading
