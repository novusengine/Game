#pragma once
#include "Game-Lib/Rendering/Animation/Pose/PoseSourceStore.h"
#include "Game-Lib/Rendering/Animation/Playback/DirectClipPlayer.h"
#include "Game-Lib/Rendering/Scene/RenderSceneHandles.h"

#include <Renderer/DescriptorSet.h>
#include <Renderer/Descriptors/ComputePipelineDesc.h>
#include <Renderer/GPUVector.h>

#include <vector>
#include <memory>

class GameRenderer;

namespace ModelLoading { class ModelGeometryStorage; }
namespace RenderScenes { class RenderScene; }
namespace Renderer { class RenderGraph; class Renderer; }

namespace Animation
{
    struct ModelDeformationRecord
    {
        u32 positionOffset = RenderScenes::INVALID_SCENE_INDEX;
        u32 vertexAttributeOffset = RenderScenes::INVALID_SCENE_INDEX;
        u32 vertexCount = 0;
        u32 poseGeneration = 0;
    };
    static_assert(sizeof(ModelDeformationRecord) == 16);

    struct ModelDeformationWork
    {
        vec4 positionDecodeOffset;
        vec4 positionDecodeExtent;
        u32 sourcePositionOffset = 0;
        u32 sourceVertexAttributeOffset = 0;
        u32 sourceSkinningOffset = 0;
        u32 vertexCount = 0;
        u32 paletteOffset = 0;
        u32 paletteCount = 0;
        u32 outputPositionOffset = 0;
        u32 outputVertexAttributeOffset = 0;
    };
    static_assert(sizeof(ModelDeformationWork) == 64);

    struct ModelDeformationStats
    {
        u32 poseSources = 0;
        u32 requests = 0;
        u32 deduplicatedRequests = 0;
        u32 dispatches = 0;
        u32 paletteMatrices = 0;
        u32 deformedVertices = 0;
        u32 queueOverflows = 0;
        u32 dispatchOverflows = 0;
        u32 paletteOverflows = 0;
        u32 deformationArenaOverflows = 0;
        u32 staticFallbacks = 0;
        u32 stalePoseSources = 0;
        u32 incompatibleSkeletons = 0;
    };

    class ModelDeformationSystem
    {
      public:
        ModelDeformationSystem(Renderer::Renderer* renderer, GameRenderer* gameRenderer, bool validateTransfers = false);

        PoseSourceHandle CreateRestPose(AnimationLoading::SkeletonHandle skeleton, const AnimationLoading::SkeletonAssetView& skeletonView) { return _poseSources.CreateRestPose(skeleton, skeletonView); }
        bool DestroyPoseSource(PoseSourceHandle handle) { return _poseSources.Destroy(handle); }
        bool SetLocalPose(PoseSourceHandle handle, std::span<const FileFormat::Animation::LocalTransform> localPose, const AnimationLoading::SkeletonAssetView& skeletonView) { return _poseSources.SetLocalPose(handle, localPose, skeletonView); }
        const PoseSource* GetPoseSource(PoseSourceHandle handle) const { return _poseSources.Get(handle); }
        DirectPlaybackHandle CreateDirectPlayback(AnimationLoading::SkeletonHandle skeleton, AnimationLoading::AnimationClipHandle clip, const AnimationLoading::SkeletonAssetView& skeletonView, const AnimationLoading::AnimationClipAssetView& clipView) { return _directPlayback->Create(skeleton, clip, skeletonView, clipView); }
        bool DestroyDirectPlayback(DirectPlaybackHandle handle) { return _directPlayback->Destroy(handle); }
        bool Play(DirectPlaybackHandle handle) { return _directPlayback->Play(handle); }
        bool Pause(DirectPlaybackHandle handle) { return _directPlayback->Pause(handle); }
        bool Stop(DirectPlaybackHandle handle) { return _directPlayback->Stop(handle); }
        bool Seek(DirectPlaybackHandle handle, f64 timeSeconds) { return _directPlayback->Seek(handle, timeSeconds); }
        bool SetLooping(DirectPlaybackHandle handle, bool looping) { return _directPlayback->SetLooping(handle, looping); }
        bool SetPlaybackRate(DirectPlaybackHandle handle, f32 playbackRate) { return _directPlayback->SetPlaybackRate(handle, playbackRate); }
        bool GetPlaybackState(DirectPlaybackHandle handle, DirectPlaybackState& state) const { return _directPlayback->GetState(handle, state); }
        PoseSourceHandle GetPlaybackPoseSource(DirectPlaybackHandle handle) const { return _directPlayback->GetPoseSource(handle); }
        void AdvancePlayback(f32 deltaTime) { _directPlayback->Advance(deltaTime); }
        const DirectPlaybackStats& GetPlaybackStats() const { return _directPlayback->GetStats(); }

        void Prepare(RenderScenes::RenderScene& scene, const ModelLoading::ModelGeometryStorage& geometry, const AnimationLoading::AnimationAssetRegistry& assets);
        void Upload();
        void AddPass(Renderer::RenderGraph* renderGraph, const ModelLoading::ModelGeometryStorage& geometry);

        const Renderer::GPUVector<ModelDeformationRecord>& GetRecords() const { return _records; }
        const Renderer::GPUVector<vec4>& GetPositions() const { return _positions; }
        const Renderer::GPUVector<uvec4>& GetVertexAttributes() const { return _vertexAttributes; }
        const ModelDeformationStats& GetStats() const { return _stats; }
        const PoseSourceStore& GetPoseSources() const { return _poseSources; }

      private:
        Renderer::Renderer* _renderer = nullptr;
        GameRenderer* _gameRenderer = nullptr;
        Renderer::DescriptorSet _descriptorSet;
        Renderer::ComputePipelineID _pipeline = Renderer::ComputePipelineID::Invalid();
        PoseSourceStore _poseSources;
        std::unique_ptr<DirectClipPlayer> _directPlayback;
        Renderer::GPUVector<AffineFloat3x4> _palettes;
        Renderer::GPUVector<ModelDeformationWork> _work;
        Renderer::GPUVector<ModelDeformationRecord> _records;
        Renderer::GPUVector<vec4> _positions;
        Renderer::GPUVector<uvec4> _vertexAttributes;
        std::vector<RenderScenes::ModelInstanceHandle> _instanceScratch;
        ModelDeformationStats _stats;
        u32 _queueCapacity = 0;
        u32 _dispatchCapacity = 0;
        u32 _paletteCapacity = 0;
        u32 _vertexCapacity = 0;
    };
} // namespace Animation
