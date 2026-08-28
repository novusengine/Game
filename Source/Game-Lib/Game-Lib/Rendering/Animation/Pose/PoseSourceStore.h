#pragma once
#include "Game-Lib/Rendering/Animation/Asset/AnimationAssetRegistry.h"
#include "Game-Lib/Rendering/Animation/Pose/AnimationPose.h"

#include <span>
#include <vector>

namespace Animation
{
    class PoseSourceHandle
    {
      public:
        constexpr PoseSourceHandle() = default;
        explicit constexpr operator bool() const { return _value != 0; }
        constexpr bool operator==(const PoseSourceHandle&) const = default;
        constexpr u64 GetRawValue() const { return _value; }

      private:
        friend class PoseSourceStore;
        explicit constexpr PoseSourceHandle(u64 value) : _value(value) {}
        constexpr u32 GetSlot() const { return static_cast<u32>(_value) - 1u; }
        constexpr u32 GetGeneration() const { return static_cast<u32>(_value >> 32u); }
        static constexpr PoseSourceHandle Create(u32 slot, u32 generation) { return PoseSourceHandle((static_cast<u64>(generation) << 32u) | (static_cast<u64>(slot) + 1u)); }
        u64 _value = 0;
    };

    struct PoseSource
    {
        AnimationLoading::SkeletonHandle skeleton;
        std::vector<FileFormat::Animation::LocalTransform> localPose;
        std::vector<AffineFloat3x4> palette;
        u64 poseGeneration = 1;
    };

    struct PoseSourceStoreStats
    {
        u32 liveSources = 0;
        u32 freeSlots = 0;
        u32 staleHandleRejects = 0;
        u64 poseEvaluations = 0;
    };

    class PoseSourceStore
    {
      public:
        PoseSourceHandle CreateRestPose(AnimationLoading::SkeletonHandle skeleton, const AnimationLoading::SkeletonAssetView& skeletonView);
        bool Destroy(PoseSourceHandle handle);
        bool SetLocalPose(PoseSourceHandle handle, std::span<const FileFormat::Animation::LocalTransform> localPose, const AnimationLoading::SkeletonAssetView& skeletonView);
        const PoseSource* Get(PoseSourceHandle handle) const;
        PoseSourceStoreStats GetStats() const;

      private:
        struct Slot
        {
            PoseSource source;
            u32 generation = 1;
            bool live = false;
        };

        Slot* GetSlot(PoseSourceHandle handle);
        const Slot* GetSlot(PoseSourceHandle handle) const;

        std::vector<Slot> _slots;
        std::vector<u32> _freeSlots;
        mutable u32 _staleHandleRejects = 0;
        u64 _poseEvaluations = 0;
    };
} // namespace Animation
