#pragma once

#include "AnimationControllerRuntime.h"
#include "Game-Lib/Rendering/Animation/Asset/AnimationAssetRegistry.h"
#include "Game-Lib/Rendering/Animation/Pose/AnimationClipSampler.h"
#include "Game-Lib/Rendering/Animation/Pose/AnimationIK.h"
#include "Game-Lib/Rendering/Animation/Pose/AnimationRetarget.h"
#include "Game-Lib/Rendering/Animation/Pose/PoseSourceStore.h"

#include <robinhood/robinhood.h>

#include <memory>
#include <vector>

namespace Animation
{
    class ControllerPlaybackHandle
    {
      public:
        constexpr ControllerPlaybackHandle() = default;
        explicit constexpr operator bool() const { return _value != 0; }
        constexpr bool operator==(const ControllerPlaybackHandle&) const = default;
        constexpr u64 GetRawValue() const { return _value; }

      private:
        friend class ControllerPosePlayer;
        explicit constexpr ControllerPlaybackHandle(u64 value) : _value(value) {}
        constexpr u32 GetSlot() const { return static_cast<u32>(_value) - 1u; }
        constexpr u32 GetGeneration() const { return static_cast<u32>(_value >> 32u); }
        static constexpr ControllerPlaybackHandle Create(u32 slot, u32 generation) { return ControllerPlaybackHandle((static_cast<u64>(generation) << 32u) | (static_cast<u64>(slot) + 1u)); }
        u64 _value = 0;
    };

    struct ControllerPosePlayerStats
    {
        u64 advances = 0;
        u64 poseRequests = 0;
        u64 poseEvaluations = 0;
        u64 blendedSamples = 0;
        u64 layerOperations = 0;
        u64 ikOperations = 0;
        u64 ikClampedGoals = 0;
        u64 snapshotBlends = 0;
        u64 failures = 0;
        u64 familyConversions = 0;
        u64 cachedPoseHits = 0;
        u32 lastActiveDomains = 0;
        u32 peakActiveDomains = 0;
    };

    // Connects animation graph pose recipes to clip sampling and shared pose sources.
    // It owns the resolved clip set and preserves transition continuity while poses are demanded.
    class ControllerPosePlayer
    {
      public:
        ControllerPosePlayer(PoseSourceStore* poseSources, AnimationLoading::AnimationAssetRegistry* assets) : _poseSources(poseSources), _assets(assets) {}
        ControllerPlaybackHandle Create(AnimationLoading::SkeletonHandle skeleton, AnimationLoading::AnimationSetHandle animationSet, const AnimationLoading::SkeletonAssetView& skeletonView, const AnimationLoading::AnimationSetAssetView& setView, std::shared_ptr<const CompiledAnimationGraph> graph, u64 stableSeed, FileFormat::AssetID retargetProfileAssetID = FileFormat::INVALID_ASSET_ID);
        bool Destroy(ControllerPlaybackHandle handle);
        void Advance(f64 deltaSeconds, bool applyTimeScale = true);
        bool RequestPose(PoseSourceHandle poseSource);
        PoseSourceHandle GetPoseSource(ControllerPlaybackHandle handle) const;
        AnimationParameterHandle ResolveParameter(ControllerPlaybackHandle handle, u64 nameHash) const;
        bool SetBool(AnimationParameterHandle handle, bool value) { return _controllers.SetBool(handle, value); }
        bool SetInt(AnimationParameterHandle handle, i32 value) { return _controllers.SetInt(handle, value); }
        bool SetFloat(AnimationParameterHandle handle, f32 value) { return _controllers.SetFloat(handle, value); }
        bool SetFloat3(AnimationParameterHandle handle, const vec3& value) { return _controllers.SetFloat3(handle, value); }
        bool Trigger(AnimationParameterHandle handle) { return _controllers.Trigger(handle); }
        bool QueueAction(ControllerPlaybackHandle handle, u64 semanticID, u32 payload = 0);
        bool GetState(ControllerPlaybackHandle handle, AnimationControllerState& state) const;
        bool GetParameterValues(ControllerPlaybackHandle handle, std::vector<vec3>& values) const;
        const ControllerPosePlayerStats& GetStats() const { return _stats; }
        AnimationControllerStats GetControllerStats() const { return _controllers.GetStats(); }

      private:
        struct SourceDomain
        {
            AnimationLoading::AnimationAssetRegistry* assets = nullptr;
            AnimationLoading::SkeletonHandle skeleton;
            AnimationLoading::RigFamilyHandle family;
            FileFormat::AssetID assetID = FileFormat::INVALID_ASSET_ID;
            FileFormat::Animation::PoseDomainType type = FileFormat::Animation::PoseDomainType::Skeleton;
            FamilyPoseBinding binding;
            std::vector<FileFormat::Animation::SkeletonJoint> samplingJoints;
            ~SourceDomain() { if (assets) { if (skeleton) assets->Release(skeleton); if (family) assets->Release(family); } }
        };
        struct ClipRecord
        {
            FileFormat::AssetID assetID = FileFormat::INVALID_ASSET_ID;
            AnimationLoading::AnimationClipHandle handle;
            std::shared_ptr<SourceDomain> domain;
        };
        struct MaskRecord
        {
            FileFormat::AssetID assetID = FileFormat::INVALID_ASSET_ID;
            AnimationLoading::BoneMaskHandle handle;
            std::vector<f32> weights;
        };
        struct IKRigRecord
        {
            FileFormat::AssetID assetID = FileFormat::INVALID_ASSET_ID;
            AnimationLoading::IKRigHandle handle;
        };
        struct Slot
        {
            AnimationLoading::SkeletonHandle skeleton;
            AnimationLoading::AnimationSetHandle animationSet;
            AnimationLoading::RigFamilyHandle targetFamily;
            AnimationLoading::RetargetProfileHandle retargetProfile;
            AnimatorHandle animator;
            PoseSourceHandle poseSource;
            std::vector<ClipRecord> clips;
            std::vector<MaskRecord> masks;
            std::vector<IKRigRecord> ikRigs;
            std::vector<AnimationClipDomainRecord> clipDomains;
            FamilyPoseBinding targetBinding;
            FamilyPoseBridge bridge;
            bool hasBridge = false;
            bool correctRetargetChains = false;
            u32 familyJointCount = 0;
            std::vector<FileFormat::Animation::LocalTransform> localPose;
            std::vector<FileFormat::Animation::LocalTransform> continuityPose;
            std::vector<FileFormat::Animation::LocalTransform> samplePose;
            std::vector<FileFormat::Animation::LocalTransform> ikBestPose;
            IKPoseWorkspace ikWorkspace;
            u64 lastRecipeHash = 0;
            u32 generation = 1;
            bool live = false;
        };

        Slot* GetSlot(ControllerPlaybackHandle handle);
        const Slot* GetSlot(ControllerPlaybackHandle handle) const;
        const AnimationLoading::AnimationClipAssetView* FindClip(const Slot& slot, FileFormat::AssetID assetID) const;
        bool SampleTargetPose(Slot& slot, std::span<const PoseRecipeSample> samples, const AnimationLoading::SkeletonAssetView& target, std::vector<FileFormat::Animation::LocalTransform>& output, std::span<const PoseRecipeLayer> sourceLayers = {});
        const MaskRecord* FindMask(const Slot& slot, FileFormat::AssetID assetID) const;
        const IKRigRecord* FindIKRig(const Slot& slot, FileFormat::AssetID assetID) const;
        AnimationControllerStore _controllers;
        PoseSourceStore* _poseSources = nullptr;
        AnimationLoading::AnimationAssetRegistry* _assets = nullptr;
        std::vector<Slot> _slots;
        std::vector<u32> _freeSlots;
        robin_hood::unordered_flat_map<u64, u32> _poseToController;
        ControllerPosePlayerStats _stats;
    };
}
