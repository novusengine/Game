#pragma once

#include "Game-Lib/Rendering/Model/View/ModelTransparentWorkResources.h"

#include <Renderer/DescriptorSet.h>
#include <Renderer/Descriptors/ComputePipelineDesc.h>
#include <Renderer/Descriptors/GraphicsPipelineDesc.h>

#include <array>
#include <robinhood/robinhood.h>

struct RenderResources;
class GameRenderer;

namespace MaterialLoading { class MaterialStorage; }
namespace ModelLoading { class ModelGeometryStorage; }
namespace ModelView { class ModelViewState; }
namespace RenderScenes { class RenderScene; class RenderView; }
namespace Renderer { class RenderGraph; class Renderer; }

namespace ModelPipeline
{
    // Owns GPU-side transparent model work generation, opaque-depth culling, and grouped forward raster pipelines.
    // The pass keeps transparent meshlets GPU-driven while sharing the offline-cooked Material execution groups.
    class ModelTransparentPass
    {
      public:
        ModelTransparentPass(Renderer::Renderer* renderer, GameRenderer* gameRenderer);

        bool Upload(const ModelView::ModelViewState& viewState, const ModelView::ModelTransparentWorkResources& work,
                    const ModelLoading::ModelGeometryStorage& geometry, const RenderScenes::RenderScene& scene);
        void AddCullPass(Renderer::RenderGraph* renderGraph, RenderResources& resources,
                         const RenderScenes::RenderView& view, const ModelView::ModelViewState& viewState,
                         ModelView::ModelTransparentWorkResources& work,
                         const ModelLoading::ModelGeometryStorage& geometry,
                         const MaterialLoading::MaterialStorage& materials,
                         const RenderScenes::RenderScene& scene, u8 frameIndex);
        void AddRasterPass(Renderer::RenderGraph* renderGraph, RenderResources& resources,
                           const RenderScenes::RenderView& view,
                           const ModelView::ModelTransparentWorkResources& work,
                           const ModelLoading::ModelGeometryStorage& geometry,
                           const MaterialLoading::MaterialStorage& materials,
                           const RenderScenes::RenderScene& scene, u8 frameIndex);

      private:
        bool Bind(Renderer::DescriptorSet& set, StringUtils::StringHash name, Renderer::BufferID buffer,
                  Renderer::BufferID& current);

        Renderer::Renderer* _renderer = nullptr;
        GameRenderer* _gameRenderer = nullptr;
        Renderer::DescriptorSet _beginSet;
        Renderer::DescriptorSet _expandSet;
        Renderer::DescriptorSet _expandFinalizeSet;
        Renderer::DescriptorSet _cullSet;
        Renderer::DescriptorSet _finalizeSet;
        Renderer::DescriptorSet _rasterSet;
        Renderer::ComputePipelineID _beginPipeline = Renderer::ComputePipelineID::Invalid();
        Renderer::ComputePipelineID _expandPipeline = Renderer::ComputePipelineID::Invalid();
        Renderer::ComputePipelineID _expandFinalizePipeline = Renderer::ComputePipelineID::Invalid();
        Renderer::ComputePipelineID _cullPipeline = Renderer::ComputePipelineID::Invalid();
        Renderer::ComputePipelineID _finalizePipeline = Renderer::ComputePipelineID::Invalid();
        std::array<Renderer::GraphicsPipelineID, ModelView::MODEL_TRANSPARENT_BIN_COUNT> _rasterPipelines = {};
        std::array<bool, ModelView::MODEL_TRANSPARENT_BIN_COUNT> _activeBins = {};
        robin_hood::unordered_flat_map<u64, Renderer::BufferID> _bindings;
        u32 _generation = 0;
    };
} // namespace ModelPipeline
