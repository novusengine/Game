#pragma once
#include "Game-Lib/Rendering/Animation/Pose/PoseSourceStore.h"
#include "Game-Lib/Rendering/Animation/Playback/DirectClipPlayer.h"
#include "Game-Lib/Rendering/Animation/Controller/ControllerPosePlayer.h"
#include "Game-Lib/Rendering/Scene/RenderSceneHandles.h"

#include <Renderer/DescriptorSet.h>
#include <Renderer/Descriptors/ComputePipelineDesc.h>
#include <Renderer/GPUVector.h>
#include <robinhood/robinhood.h>

#include <vector>
#include <memory>

class GameRenderer;

namespace ModelLoading { class ModelGeometryStorage; }
namespace RenderScenes { class RenderScene; }
namespace RenderScenes { class RenderView; }
struct RenderResources;
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
        u32 viewRequests = 0;
        u32 transientViewRequests = 0;
        u32 shadowViewRequests = 0;
        u32 frustumRejectedInstances = 0;
        u32 selectedSegmentBounds = 0;
        u32 conservativeBoundsFallbacks = 0;
        u32 debugActualBounds = 0;
        u32 boundsViolations = 0;
        u32 demandedLODs = 0;
    };

    // Coordinates animation playback, pose demand, and GPU skinning for model instances.
    // Per-frame work is deduplicated by pose and model LOD before compute deformation runs.
    class ModelDeformationSystem
    {
      public:
        ModelDeformationSystem(Renderer::Renderer* renderer, GameRenderer* gameRenderer, bool validateTransfers = false);

        PoseSourceHandle CreateRestPose(AnimationLoading::SkeletonHandle skeleton, const AnimationLoading::SkeletonAssetView& skeletonView) { return _poseSources.CreateRestPose(skeleton, skeletonView); }
        bool DestroyPoseSource(PoseSourceHandle handle) { return _poseSources.Destroy(handle); }
        bool SetLocalPose(PoseSourceHandle handle, std::span<const FileFormat::Animation::LocalTransform> localPose, const AnimationLoading::SkeletonAssetView& skeletonView) { return _poseSources.SetLocalPose(handle, localPose, skeletonView); }
        bool InvalidatePoseSource(PoseSourceHandle handle) { return _poseSources.Invalidate(handle); }
        const PoseSource* GetPoseSource(PoseSourceHandle handle) const { return _poseSources.Get(handle); }
        PoseAttachmentHandle CreateAttachment(PoseSourceHandle parent, const AnimationLoading::SkeletonAssetView& skeletonView, u64 semanticID, bool visualOnly = true, const vec3& childBoundsExtent = {}) { return _poseSources.CreateAttachment(parent, skeletonView, semanticID, visualOnly, childBoundsExtent); }
        bool DestroyAttachment(PoseAttachmentHandle handle) { return _poseSources.DestroyAttachment(handle); }
        const PoseAttachment* GetAttachment(PoseAttachmentHandle handle) const { return _poseSources.GetAttachment(handle); }
        bool BindModelAttachment(PoseAttachmentHandle attachment, RenderScenes::RenderScene* scene, RenderScenes::ModelInstanceHandle parent, RenderScenes::ModelInstanceHandle child, const mat4x4& childLocalTransform);
        bool SetModelAttachmentLocalTransform(PoseAttachmentHandle attachment, const mat4x4& childLocalTransform);
        bool UnbindModelAttachment(PoseAttachmentHandle attachment);
        void BeginPoseQueries(u64 frame) { _poseSources.BeginPoseQueryFrame(frame); }
        PoseQueryHandle RequestJoint(PoseSourceHandle poseSource, const AnimationLoading::SkeletonAssetView& skeletonView, u64 semanticID) { return _poseSources.RequestJoint(poseSource, skeletonView, semanticID); }
        const PoseQueryResult* GetPoseQuery(PoseQueryHandle handle) const { return _poseSources.GetPoseQuery(handle); }
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
        ControllerPlaybackHandle CreateControllerPlayback(AnimationLoading::SkeletonHandle skeleton, AnimationLoading::AnimationSetHandle animationSet, const AnimationLoading::SkeletonAssetView& skeletonView, const AnimationLoading::AnimationSetAssetView& setView, std::shared_ptr<const CompiledAnimationGraph> graph, u64 stableSeed, FileFormat::AssetID retargetProfileAssetID = FileFormat::INVALID_ASSET_ID) { return _controllerPlayback->Create(skeleton, animationSet, skeletonView, setView, std::move(graph), stableSeed, retargetProfileAssetID); }
        bool DestroyControllerPlayback(ControllerPlaybackHandle handle) { return _controllerPlayback->Destroy(handle); }
        void StepPreviewControllers(f64 deltaSeconds) { _controllerPlayback->Advance(deltaSeconds, false); }
        PoseSourceHandle GetControllerPoseSource(ControllerPlaybackHandle handle) const { return _controllerPlayback->GetPoseSource(handle); }
        AnimationParameterHandle ResolveControllerParameter(ControllerPlaybackHandle handle, u64 nameHash) const { return _controllerPlayback->ResolveParameter(handle, nameHash); }
        bool SetControllerFloat(AnimationParameterHandle handle, f32 value) { return _controllerPlayback->SetFloat(handle, value); }
        bool SetControllerFloat3(AnimationParameterHandle handle, const vec3& value) { return _controllerPlayback->SetFloat3(handle, value); }
        bool SetControllerBool(AnimationParameterHandle handle, bool value) { return _controllerPlayback->SetBool(handle, value); }
        bool TriggerController(AnimationParameterHandle handle) { return _controllerPlayback->Trigger(handle); }
        bool QueueControllerAction(ControllerPlaybackHandle handle, u64 semanticID, u32 payload = 0) { return _controllerPlayback->QueueAction(handle, semanticID, payload); }
        bool GetControllerState(ControllerPlaybackHandle handle, AnimationControllerState& state) const { return _controllerPlayback->GetState(handle, state); }
        bool GetControllerParameterValues(ControllerPlaybackHandle handle, std::vector<vec3>& values) const { return _controllerPlayback->GetParameterValues(handle, values); }
        const ControllerPosePlayerStats& GetControllerPlaybackStats() const { return _controllerPlayback->GetStats(); }
        AnimationControllerStats GetControllerCoreStats() const { return _controllerPlayback->GetControllerStats(); }
        void AdvanceControllerPlayback(f32 deltaTime) { _controllerPlayback->Advance(deltaTime); }

        void Prepare(std::span<RenderScenes::RenderScene* const> scenes, std::span<RenderScenes::RenderView* const> views, const RenderResources& resources, i32 forcedLOD, const ModelLoading::ModelGeometryStorage& geometry, AnimationLoading::AnimationAssetRegistry& assets);
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
        std::unique_ptr<ControllerPosePlayer> _controllerPlayback;
        Renderer::GPUVector<AffineFloat3x4> _palettes;
        Renderer::GPUVector<ModelDeformationWork> _work;
        Renderer::GPUVector<ModelDeformationRecord> _records;
        Renderer::GPUVector<vec4> _positions;
        Renderer::GPUVector<uvec4> _vertexAttributes;
        std::vector<RenderScenes::ModelInstanceHandle> _instanceScratch;
        robin_hood::unordered_flat_map<FileFormat::AssetID, AnimationLoading::AnimationBoundsHandle> _boundsAssets;
        ModelDeformationStats _stats;
        u32 _queueCapacity = 0;
        u32 _dispatchCapacity = 0;
        u32 _paletteCapacity = 0;
        u32 _vertexCapacity = 0;
        u64 _demandFrame = 0;
        struct ModelAttachmentBinding
        {
            PoseAttachmentHandle attachment;
            RenderScenes::RenderScene* scene = nullptr;
            RenderScenes::ModelInstanceHandle parent = RenderScenes::InvalidModelInstanceHandle();
            RenderScenes::ModelInstanceHandle child = RenderScenes::InvalidModelInstanceHandle();
            mat4x4 childLocalTransform = mat4x4(1.0f);
        };
        std::vector<ModelAttachmentBinding> _modelAttachments;
    };
} // namespace Animation
