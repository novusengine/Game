#pragma once
#include "Game-Lib/Rendering/Animation/Asset/AnimationAssetRegistry.h"
#include "Game-Lib/Rendering/Animation/Pose/AnimationPose.h"

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
    };

    class SkeletonDebugDraw
    {
      public:
        SkeletonDebugDraw(DebugRenderer* debugRenderer, AnimationLoading::AnimationAssetRegistry* assets);
        ~SkeletonDebugDraw();

        void Update();
        void SetCurrentLocalPose(std::span<const FileFormat::Animation::LocalTransform> localPose);
        void ClearCurrentLocalPose();
        const SkeletonDebugDrawStats& GetStats() const { return _stats; }

      private:
        bool EnsureSkeletonLoaded();
        bool EvaluateRequestedPose();
        void DrawSkeleton(const mat4x4& worldTransform);
        void DrawLabel(u64 value, const vec3& origin, const vec3& right, const vec3& up, Color color, u32 maximumDigits);
        void EmitLine(const vec3& from, const vec3& to, Color color);
        void ReleaseSkeleton();

        DebugRenderer* _debugRenderer = nullptr;
        AnimationLoading::AnimationAssetRegistry* _assets = nullptr;
        AnimationLoading::SkeletonHandle _skeletonHandle;
        FileFormat::AssetID _skeletonPathID = FileFormat::INVALID_ASSET_ID;
        const AnimationLoading::SkeletonAssetView* _skeleton = nullptr;
        PoseEvaluationResult _evaluatedPose;
        std::vector<FileFormat::Animation::LocalTransform> _currentLocalPose;
        std::string _loadedPath;
        SkeletonDebugDrawStats _stats;
        i32 _lastLoggedSelectedJoint = std::numeric_limits<i32>::min();
        bool _reportedCapacity = false;
        bool _wasEnabled = false;
        bool _reportedReady = false;
    };
} // namespace Animation
