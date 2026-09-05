#pragma once
#include "Game-Lib/Rendering/Animation/Asset/AnimationAssetRegistry.h"
#include "Game-Lib/Rendering/Animation/Pose/AnimationIK.h"
#include "Game-Lib/Rendering/Animation/Pose/AnimationPose.h"

#include <span>
#include <limits>
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

    class PoseAttachmentHandle
    {
      public:
        constexpr PoseAttachmentHandle() = default;
        explicit constexpr operator bool() const { return _value != 0; }
        constexpr bool operator==(const PoseAttachmentHandle&) const = default;

      private:
        friend class PoseSourceStore;
        explicit constexpr PoseAttachmentHandle(u64 value) : _value(value) {}
        constexpr u32 GetSlot() const { return static_cast<u32>(_value) - 1u; }
        constexpr u32 GetGeneration() const { return static_cast<u32>(_value >> 32u); }
        static constexpr PoseAttachmentHandle Create(u32 slot, u32 generation) { return PoseAttachmentHandle((static_cast<u64>(generation) << 32u) | (static_cast<u64>(slot) + 1u)); }
        u64 _value = 0;
    };

    struct PoseQueryHandle
    {
        u64 frame = 0;
        u32 index = std::numeric_limits<u32>::max();
        explicit operator bool() const { return frame != 0 && index != std::numeric_limits<u32>::max(); }
    };

    struct PoseIKDebugOperation
    {
        std::array<u32, 16> jointIndices = {};
        u32 jointCount = 0;
        vec3 goalModel = {};
        vec3 poleModel = {};
        f32 maximumReach = 0.0f;
        f32 finalError = 0.0f;
        IKSolveStatus status = IKSolveStatus::InvalidChain;
        bool clamped = false;
        bool jointLimitClamped = false;
    };

    struct PoseSource
    {
        AnimationLoading::SkeletonHandle skeleton;
        std::vector<FileFormat::Animation::LocalTransform> localPose;
        std::vector<mat4a> modelTransforms;
        std::vector<mat4a> previousModelTransforms;
        std::vector<AffineFloat3x4> palette;
        std::vector<f32> debugJointWeights;
        std::array<PoseIKDebugOperation, 4> debugIKOperations = {};
        u32 debugIKOperationCount = 0;
        u64 poseGeneration = 1;
        u64 sampleTimeMicroseconds = 0;
        FileFormat::AssetID boundsClipAssetID = FileFormat::INVALID_ASSET_ID;
        vec3 conservativeBoundsExpansion = {};
        vec3 attachmentBoundsExpansion = {};
        bool supportsTightAnimationBounds = false;
        u64 lastDemandFrame = 0;
        bool participatedPreviousFrame = false;
        bool participatedCurrentFrame = false;
        bool motionValid = false;
    };

    struct PoseAttachment
    {
        PoseSourceHandle parent;
        u64 semanticID = 0;
        u32 jointIndex = 0;
        mat4a currentTransform = mat4a(1.0f);
        mat4a previousTransform = mat4a(1.0f);
        u64 parentPoseGeneration = 0;
        u64 lastResolvedFrame = 0;
        vec3 conservativeBoundsExpansion = {};
        bool visualOnly = true;
        bool valid = false;
        bool motionValid = false;
    };

    struct PoseQueryResult
    {
        PoseSourceHandle poseSource;
        u64 jointSemanticID = 0;
        u32 jointIndex = 0;
        mat4a modelTransform = mat4a(1.0f);
        bool valid = false;
    };

    struct PoseSourceStoreStats
    {
        u32 liveSources = 0;
        u32 freeSlots = 0;
        u32 staleHandleRejects = 0;
        u64 poseEvaluations = 0;
        u64 invalidations = 0;
        u32 liveAttachments = 0;
        u64 attachmentResolves = 0;
        u64 sparseQueryJoints = 0;
        u64 deduplicatedQueries = 0;
        u64 fullPoseQueryPromotions = 0;
    };

    // Owns generation-checked local poses and their evaluated GPU skinning palettes.
    // Demand tracking lets shared poses update only while a visible model needs them.
    class PoseSourceStore
    {
      public:
        PoseSourceHandle CreateRestPose(AnimationLoading::SkeletonHandle skeleton, const AnimationLoading::SkeletonAssetView& skeletonView);
        bool Destroy(PoseSourceHandle handle);
        bool SetLocalPose(PoseSourceHandle handle, std::span<const FileFormat::Animation::LocalTransform> localPose, const AnimationLoading::SkeletonAssetView& skeletonView, u64 sampleTimeMicroseconds = 0);
        bool SetBoundsMetadata(PoseSourceHandle handle, FileFormat::AssetID clipAssetID, u64 sampleTimeMicroseconds, bool supportsTightBounds);
        bool SetConservativeBoundsExpansion(PoseSourceHandle handle, const vec3& expansion);
        bool SetDebugJointWeights(PoseSourceHandle handle, std::span<const f32> weights);
        bool SetDebugIKOperations(PoseSourceHandle handle, std::span<const PoseIKDebugOperation> operations);
        bool Invalidate(PoseSourceHandle handle);
        void BeginDemandFrame(u64 frame);
        bool MarkDemand(PoseSourceHandle handle, u64 frame);
        PoseAttachmentHandle CreateAttachment(PoseSourceHandle parent, const AnimationLoading::SkeletonAssetView& skeletonView, u64 semanticID, bool visualOnly = true, const vec3& childBoundsExtent = {});
        bool DestroyAttachment(PoseAttachmentHandle handle);
        void ResolveAttachments(u64 frame);
        const PoseAttachment* GetAttachment(PoseAttachmentHandle handle) const;
        void BeginPoseQueryFrame(u64 frame);
        PoseQueryHandle RequestJoint(PoseSourceHandle poseSource, const AnimationLoading::SkeletonAssetView& skeletonView, u64 jointSemanticID);
        void ResolvePoseQueries();
        const PoseQueryResult* GetPoseQuery(PoseQueryHandle handle) const;
        const PoseSource* Get(PoseSourceHandle handle) const;
        PoseSourceStoreStats GetStats() const;

      private:
        struct Slot
        {
            PoseSource source;
            u32 generation = 1;
            bool live = false;
        };
        struct AttachmentSlot
        {
            PoseAttachment attachment;
            FileFormat::Animation::LocalTransform localTransform;
            u32 generation = 1;
            bool live = false;
        };
        struct PendingQuery
        {
            PoseQueryResult result;
        };

        Slot* GetSlot(PoseSourceHandle handle);
        const Slot* GetSlot(PoseSourceHandle handle) const;
        AttachmentSlot* GetAttachmentSlot(PoseAttachmentHandle handle);
        const AttachmentSlot* GetAttachmentSlot(PoseAttachmentHandle handle) const;
        void RecomputeAttachmentBounds(PoseSourceHandle parent);

        std::vector<Slot> _slots;
        std::vector<u32> _freeSlots;
        std::vector<AttachmentSlot> _attachmentSlots;
        std::vector<u32> _freeAttachmentSlots;
        std::vector<PendingQuery> _queries;
        mutable u32 _staleHandleRejects = 0;
        u64 _poseEvaluations = 0;
        u64 _invalidations = 0;
        u64 _demandFrame = 0;
        u64 _queryFrame = 0;
        u64 _attachmentResolves = 0;
        u64 _sparseQueryJoints = 0;
        u64 _deduplicatedQueries = 0;
        u64 _fullPoseQueryPromotions = 0;
    };
} // namespace Animation
