#pragma once

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

        bool Upload(const ModelView::ModelViewState& viewState, ModelView::ModelViewWorkResources& work,
                    const ModelLoading::ModelGeometryStorage& geometry, const MaterialLoading::MaterialStorage& materials,
                    const RenderScenes::RenderScene& scene);
        void AddPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, const RenderScenes::RenderView& view,
                     const ModelView::ModelViewState& viewState, ModelView::ModelViewWorkResources& work,
                     const ModelLoading::ModelGeometryStorage& geometry, const MaterialLoading::MaterialStorage& materials,
                     const RenderScenes::RenderScene& scene, u8 frameIndex, bool resetHistory, i32 forcedLOD);

      private:
        bool Bind(Renderer::DescriptorSet& descriptorSet, StringUtils::StringHash name, Renderer::BufferID buffer,
                  Renderer::BufferID& current);

        Renderer::Renderer* _renderer = nullptr;
        Renderer::DescriptorSet _expandDescriptorSet;
        Renderer::DescriptorSet _expandFinalizeDescriptorSet;
        Renderer::DescriptorSet _cullDescriptorSet;
        Renderer::DescriptorSet _finalizeDescriptorSet;
        Renderer::ComputePipelineID _expandPipeline = Renderer::ComputePipelineID::Invalid();
        Renderer::ComputePipelineID _expandFinalizePipeline = Renderer::ComputePipelineID::Invalid();
        Renderer::ComputePipelineID _cullPipeline = Renderer::ComputePipelineID::Invalid();
        Renderer::ComputePipelineID _finalizePipeline = Renderer::ComputePipelineID::Invalid();
        Renderer::BufferID _expandBoundBuffers[15] = {};
        Renderer::BufferID _expandFinalizeBoundBuffers[4] = {};
        Renderer::BufferID _cullBoundBuffers[12] = {};
        Renderer::BufferID _finalizeBoundBuffers[4] = {};
        u32 _descriptorWarmupFrames = 0;
    };
} // namespace ModelPipeline
