#pragma once

#include "Game-Lib/Rendering/Animation/Asset/AnimationAssetRegistry.h"
#include "Game-Lib/Rendering/Animation/Pose/AnimationClipSampler.h"
#include "Game-Lib/Rendering/Animation/Pose/PoseSourceStore.h"

#include <robinhood/robinhood.h>

#include <vector>

namespace Animation
{
    class DirectPlaybackHandle
    {
      public:
        constexpr DirectPlaybackHandle() = default;
        explicit constexpr operator bool() const { return _value != 0; }
        constexpr bool operator==(const DirectPlaybackHandle&) const = default;
        constexpr u64 GetRawValue() const { return _value; }

      private:
        friend class DirectClipPlayer;
        explicit constexpr DirectPlaybackHandle(u64 value) : _value(value) {}
        constexpr u32 GetSlot() const { return static_cast<u32>(_value) - 1u; }
        constexpr u32 GetGeneration() const { return static_cast<u32>(_value >> 32u); }
        static constexpr DirectPlaybackHandle Create(u32 slot, u32 generation) { return DirectPlaybackHandle((static_cast<u64>(generation) << 32u) | (static_cast<u64>(slot) + 1u)); }
        u64 _value = 0;
    };

    struct DirectPlaybackState
    {
        f64 timeSeconds = 0.0;
        f64 durationSeconds = 0.0;
        f32 playbackRate = 1.0f;
        bool playing = false;
        bool looping = true;
    };

    struct DirectPlaybackStats
    {
        u64 controllerAdvances = 0;
        u64 poseRequests = 0;
        u64 poseEvaluations = 0;
        u64 evaluationMicroseconds = 0;
    };

    class DirectClipPlayer
    {
      public:
        DirectClipPlayer(PoseSourceStore* poseSources, AnimationLoading::AnimationAssetRegistry* assets) : _poseSources(poseSources), _assets(assets) {}

        DirectPlaybackHandle Create(AnimationLoading::SkeletonHandle skeleton, AnimationLoading::AnimationClipHandle clip, const AnimationLoading::SkeletonAssetView& skeletonView, const AnimationLoading::AnimationClipAssetView& clipView);
        bool Destroy(DirectPlaybackHandle handle);
        bool Play(DirectPlaybackHandle handle);
        bool Pause(DirectPlaybackHandle handle);
        bool Stop(DirectPlaybackHandle handle);
        bool Seek(DirectPlaybackHandle handle, f64 timeSeconds);
        bool SetLooping(DirectPlaybackHandle handle, bool looping);
        bool SetPlaybackRate(DirectPlaybackHandle handle, f32 playbackRate);
        bool GetState(DirectPlaybackHandle handle, DirectPlaybackState& state) const;
        PoseSourceHandle GetPoseSource(DirectPlaybackHandle handle) const;
        void Advance(f32 deltaTime);
        bool RequestPose(PoseSourceHandle poseSource);
        const DirectPlaybackStats& GetStats() const { return _stats; }

      private:
        struct Controller
        {
            AnimationLoading::SkeletonHandle skeleton;
            AnimationLoading::AnimationClipHandle clip;
            PoseSourceHandle poseSource;
            std::vector<FileFormat::Animation::LocalTransform> localPose;
            f64 timeSeconds = 0.0;
            f32 playbackRate = 1.0f;
            bool playing = false;
            bool looping = true;
            bool sampleDirty = true;
        };
        struct Slot
        {
            Controller controller;
            u32 generation = 1;
            bool live = false;
        };

        Slot* GetSlot(DirectPlaybackHandle handle);
        const Slot* GetSlot(DirectPlaybackHandle handle) const;
        f64 MapControllerTime(const Controller& controller, f64 timeSeconds) const;

        PoseSourceStore* _poseSources = nullptr;
        AnimationLoading::AnimationAssetRegistry* _assets = nullptr;
        std::vector<Slot> _slots;
        std::vector<u32> _freeSlots;
        robin_hood::unordered_flat_map<u64, u32> _poseToController;
        DirectPlaybackStats _stats;
    };
}
