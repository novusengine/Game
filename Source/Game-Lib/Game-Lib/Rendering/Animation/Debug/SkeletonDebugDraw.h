#pragma once
#include "Game-Lib/Rendering/Animation/Asset/AnimationAssetRegistry.h"
#include "Game-Lib/Rendering/Animation/Pose/AnimationPose.h"
#include "Game-Lib/Rendering/Animation/Pose/AnimationRetarget.h"
#include "Game-Lib/Rendering/Animation/Pose/PoseSourceStore.h"

#include <Base/Math/Color.h>

#include <string>
#include <vector>

class DebugRenderer;

namespace Animation
{
    struct SkeletonDebugDrawStats
    {
        u32 joints = 0;
        u32 lines = 0;
        u32 droppedLines = 0;
        u32 evaluations = 0;
        bool enabled = false;
        bool loaded = false;
        bool currentPoseFallback = false;
        f32 maximumReferencePositionError = 0.0f;
        f32 maximumReferenceAngleError = 0.0f;
        bool referenceCompared = false;
    };

    // Draws skeleton poses and animation bounds through the shared debug renderer.
    // CVAR controls allow runtime inspection without changing normal animation behavior.
    class SkeletonDebugDraw
    {
      public:
        SkeletonDebugDraw(DebugRenderer* debugRenderer, AnimationLoading::AnimationAssetRegistry* assets);
        ~SkeletonDebugDraw();

        void Update();
        bool SetCurrentLocalPose(FileFormat::AssetID skeletonAssetID, std::span<const FileFormat::Animation::LocalTransform> localPose, u64 sampleTimeMicroseconds);
        void SetJointWeights(std::span<const f32> weights);
        void SetIKOperations(std::span<const PoseIKDebugOperation> operations);
        void ClearCurrentLocalPose();
        bool WantsAnimationBounds() const;
        void SetAnimationBounds(const FileFormat::Animation::Bounds& selectedWorldBounds, const FileFormat::Animation::Bounds& actualWorldBounds, bool tightSelection, bool violation);
        void ClearAnimationBounds();
        const SkeletonDebugDrawStats& GetStats() const { return _stats; }

      private:
        bool EnsureSkeletonLoaded();
        bool EvaluateRequestedPose();
        void EvaluateReference();
        void DrawSkeleton(const mat4x4& worldTransform);
        void DrawFamily(const mat4x4& worldTransform);
        void DrawBounds(const FileFormat::Animation::Bounds& bounds, Color color);
        void DrawLabel(u64 value, const vec3& origin, const vec3& right, const vec3& up, Color color, u32 maximumDigits);
        void EmitLine(const vec3& from, const vec3& to, Color color);
        void ReleaseSkeleton();

        DebugRenderer* _debugRenderer = nullptr;
        AnimationLoading::AnimationAssetRegistry* _assets = nullptr;
        AnimationLoading::SkeletonHandle _skeletonHandle;
        AnimationLoading::RigFamilyHandle _familyHandle;
        FamilyPoseBinding _familyBinding;
        AnimationLoading::AnimationClipHandle _referenceHandle;
        std::string _referencePath;
        std::vector<f32> _referenceErrors;
        u64 _sampleTimeMicroseconds = 0;
        FileFormat::AssetID _skeletonPathID = FileFormat::INVALID_ASSET_ID;
        const AnimationLoading::SkeletonAssetView* _skeleton = nullptr;
        PoseEvaluationResult _evaluatedPose;
        std::vector<FileFormat::Animation::LocalTransform> _currentLocalPose;
        std::vector<f32> _jointWeights;
        std::array<PoseIKDebugOperation, 4> _ikOperations = {};
        u32 _ikOperationCount = 0;
        std::string _loadedPath;
        SkeletonDebugDrawStats _stats;
        i32 _lastLoggedSelectedJoint = std::numeric_limits<i32>::min();
        bool _reportedCapacity = false;
        bool _wasEnabled = false;
        bool _reportedReady = false;
        FileFormat::Animation::Bounds _selectedWorldBounds;
        FileFormat::Animation::Bounds _actualWorldBounds;
        bool _hasAnimationBounds = false;
        bool _tightAnimationBounds = false;
        bool _animationBoundsViolation = false;
    };
} // namespace Animation
