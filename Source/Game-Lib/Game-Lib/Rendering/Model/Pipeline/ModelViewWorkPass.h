#pragma once

#include "Game-Lib/Rendering/Model/View/ModelViewWork.h"

#include <Renderer/DescriptorSet.h>
#include <Renderer/Descriptors/ComputePipelineDesc.h>

struct RenderResources;
class GameRenderer;

namespace MaterialLoading { class MaterialStorage; }
namespace ModelLoading { class ModelGeometryStorage; }
namespace ModelView { class ModelViewState; class ModelViewWorkResources; }
namespace RenderScenes { class RenderScene; class RenderView; }
namespace Renderer { class RenderGraph; class Renderer; }

namespace ModelPipeline
{
    // Owns the GPU-side pipeline and bindings that expand selected instances into visible meshlet queues.
    // It selects coherent LODs and rejects ineligible work before rasterization consumes indirect commands.
    class ModelViewWorkPass
    {
      public:
        ModelViewWorkPass(Renderer::Renderer* renderer, GameRenderer* gameRenderer);

        void PrepareResources(const ModelView::ModelViewState& viewState, ModelView::ModelViewWorkResources& work,
                              const RenderScenes::RenderScene& scene);
        bool Upload(const ModelView::ModelViewState& viewState, ModelView::ModelViewWorkResources& work,
                    const ModelLoading::ModelGeometryStorage& geometry,
                    const RenderScenes::RenderScene& scene);
        void AddPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, const RenderScenes::RenderView& view,
                     const ModelView::ModelViewState& viewState, ModelView::ModelViewWorkResources& work,
                     const ModelLoading::ModelGeometryStorage& geometry, const MaterialLoading::MaterialStorage& materials,
                     const RenderScenes::RenderScene& scene, u8 frameIndex, bool resetHistory, i32 forcedLOD);
        void AddPhase1Pass(Renderer::RenderGraph* renderGraph, RenderResources& resources,
                           const RenderScenes::RenderView& view, const ModelView::ModelViewState& viewState,
                           ModelView::ModelViewWorkResources& work,
                           const ModelLoading::ModelGeometryStorage& geometry,
                           const MaterialLoading::MaterialStorage& materials,
                           const RenderScenes::RenderScene& scene, u8 frameIndex, bool resetHistory, i32 forcedLOD);
        void AddPhase2Pass(Renderer::RenderGraph* renderGraph, RenderResources& resources,
                           const RenderScenes::RenderView& view, const ModelView::ModelViewState& viewState,
                           ModelView::ModelViewWorkResources& work,
                           const ModelLoading::ModelGeometryStorage& geometry,
                           const RenderScenes::RenderScene& scene, Renderer::ImageID depthPyramid, u8 frameIndex);

      private:
        struct ExpandFrameBindings
        {
            Renderer::BufferID chunkQueue = Renderer::BufferID::Invalid();
            Renderer::BufferID workStats = Renderer::BufferID::Invalid();
            Renderer::BufferID instanceVisibility = Renderer::BufferID::Invalid();
        };

        struct ExpandBindings
        {
            Renderer::BufferID viewInputs = Renderer::BufferID::Invalid();
            Renderer::BufferID lodHistory = Renderer::BufferID::Invalid();
            Renderer::BufferID modelInstances = Renderer::BufferID::Invalid();
            Renderer::BufferID modelRecords = Renderer::BufferID::Invalid();
            Renderer::BufferID modelMeshes = Renderer::BufferID::Invalid();
            Renderer::BufferID modelLODs = Renderer::BufferID::Invalid();
            Renderer::BufferID modelSubmeshes = Renderer::BufferID::Invalid();
            Renderer::BufferID geometryGroupMasks = Renderer::BufferID::Invalid();
            Renderer::BufferID materialTable = Renderer::BufferID::Invalid();
            ExpandFrameBindings frames[ModelView::MODEL_VIEW_FRAME_COUNT];
        };

        struct ExpandFinalizeFrameBindings
        {
            Renderer::BufferID workStats = Renderer::BufferID::Invalid();
            Renderer::BufferID chunkArguments = Renderer::BufferID::Invalid();
        };

        struct CullFrameBindings
        {
            Renderer::BufferID chunkQueue = Renderer::BufferID::Invalid();
            Renderer::BufferID rasterQueues[ModelView::MODEL_RASTER_CLASS_COUNT] = {};
            Renderer::BufferID workStats = Renderer::BufferID::Invalid();
            Renderer::BufferID visibilityRecords = Renderer::BufferID::Invalid();
            Renderer::BufferID cullReasons = Renderer::BufferID::Invalid();
            Renderer::BufferID meshletHistory = Renderer::BufferID::Invalid();
            Renderer::BufferID survivorQueue = Renderer::BufferID::Invalid();
        };

        struct CullBindings
        {
            Renderer::BufferID modelInstances = Renderer::BufferID::Invalid();
            Renderer::BufferID modelRecords = Renderer::BufferID::Invalid();
            Renderer::BufferID modelMeshes = Renderer::BufferID::Invalid();
            Renderer::BufferID modelMeshlets = Renderer::BufferID::Invalid();
            CullFrameBindings frames[ModelView::MODEL_VIEW_FRAME_COUNT];
        };

        struct FinalizeFrameBindings
        {
            Renderer::BufferID workStats = Renderer::BufferID::Invalid();
            Renderer::BufferID indirectArguments = Renderer::BufferID::Invalid();
            Renderer::BufferID survivorArguments = Renderer::BufferID::Invalid();
        };

        struct ReplayBindings
        {
            Renderer::BufferID modelInstances = Renderer::BufferID::Invalid();
            Renderer::BufferID modelRecords = Renderer::BufferID::Invalid();
            Renderer::BufferID modelMeshlets = Renderer::BufferID::Invalid();
            Renderer::BufferID lodHistory = Renderer::BufferID::Invalid();
            Renderer::BufferID instanceVisibility[ModelView::MODEL_VIEW_FRAME_COUNT] = {};
            Renderer::BufferID meshletHistory[ModelView::MODEL_VIEW_FRAME_COUNT] = {};
            Renderer::BufferID survivorQueues[ModelView::MODEL_VIEW_FRAME_COUNT] = {};
            Renderer::BufferID rasterQueues[ModelView::MODEL_VIEW_FRAME_COUNT][ModelView::MODEL_RASTER_CLASS_COUNT] = {};
            Renderer::BufferID visibilityRecords[ModelView::MODEL_VIEW_FRAME_COUNT] = {};
            Renderer::BufferID workStats[ModelView::MODEL_VIEW_FRAME_COUNT] = {};
        };

        struct BeginPhase2FrameBindings
        {
            Renderer::BufferID workStats = Renderer::BufferID::Invalid();
            Renderer::BufferID indirectArguments = Renderer::BufferID::Invalid();
        };

        bool Bind(Renderer::DescriptorSet& descriptorSet, StringUtils::StringHash name, Renderer::BufferID buffer,
                  Renderer::BufferID& current);

        Renderer::Renderer* _renderer = nullptr;
        Renderer::DescriptorSet _expandDescriptorSet;
        Renderer::DescriptorSet _expandFinalizeDescriptorSet;
        Renderer::DescriptorSet _cullDescriptorSet;
        Renderer::DescriptorSet _finalizeDescriptorSet;
        Renderer::DescriptorSet _replayDescriptorSet;
        Renderer::DescriptorSet _beginPhase2DescriptorSet;
        Renderer::ComputePipelineID _expandPipeline = Renderer::ComputePipelineID::Invalid();
        Renderer::ComputePipelineID _expandFinalizePipeline = Renderer::ComputePipelineID::Invalid();
        Renderer::ComputePipelineID _cullPipeline = Renderer::ComputePipelineID::Invalid();
        Renderer::ComputePipelineID _finalizePipeline = Renderer::ComputePipelineID::Invalid();
        Renderer::ComputePipelineID _replayPipeline = Renderer::ComputePipelineID::Invalid();
        Renderer::ComputePipelineID _beginPhase2Pipeline = Renderer::ComputePipelineID::Invalid();
        ExpandBindings _expandBindings;
        ExpandFinalizeFrameBindings _expandFinalizeBindings[ModelView::MODEL_VIEW_FRAME_COUNT];
        CullBindings _cullBindings;
        FinalizeFrameBindings _finalizeBindings[ModelView::MODEL_VIEW_FRAME_COUNT];
        ReplayBindings _replayBindings;
        BeginPhase2FrameBindings _beginPhase2Bindings[ModelView::MODEL_VIEW_FRAME_COUNT];
        u32 _descriptorWarmupFrames = 0;
    };
} // namespace ModelPipeline
