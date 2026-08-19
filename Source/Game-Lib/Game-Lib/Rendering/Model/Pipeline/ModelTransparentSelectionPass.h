#pragma once

#include "Game-Lib/Rendering/Model/View/ModelTransparentWorkResources.h"

#include <Renderer/DescriptorSet.h>
#include <Renderer/Descriptors/ComputePipelineDesc.h>
#include <Renderer/Descriptors/GraphicsPipelineDesc.h>

#include <array>
#include <robinhood/robinhood.h>

class GameRenderer;
struct RenderResources;
namespace MaterialLoading { class MaterialStorage; }
namespace ModelLoading { class ModelGeometryStorage; }
namespace RenderScenes { class RenderScene; class RenderView; }
namespace Renderer { class RenderGraph; class Renderer; }

namespace ModelPipeline
{
    // Owns the GPU-side selected-transparent depth and outline work for one model View.
    // Its passes exist only while the Scene contains highlighted transparent geometry.
    class ModelTransparentSelectionPass
    {
      public:
        ModelTransparentSelectionPass(Renderer::Renderer* renderer, GameRenderer* gameRenderer);

        bool Upload(const ModelView::ModelTransparentWorkResources& work,
                    const ModelLoading::ModelGeometryStorage& geometry, const RenderScenes::RenderScene& scene);
        void AddDepthPass(Renderer::RenderGraph* renderGraph, RenderResources& resources,
                          const RenderScenes::RenderView& view,
                          const ModelView::ModelTransparentWorkResources& work,
                          const ModelLoading::ModelGeometryStorage& geometry,
                          const MaterialLoading::MaterialStorage& materials, const RenderScenes::RenderScene& scene,
                          Renderer::DepthImageID selectionDepth, u8 frameIndex);
        void AddOutlinePass(Renderer::RenderGraph* renderGraph, const RenderScenes::RenderView& view,
                            Renderer::ImageID revealage, Renderer::DepthImageID selectionDepth, u8 frameIndex);

      private:
        bool Bind(StringUtils::StringHash name, Renderer::BufferID buffer);

        Renderer::Renderer* _renderer = nullptr;
        GameRenderer* _gameRenderer = nullptr;
        Renderer::DescriptorSet _depthSet;
        Renderer::DescriptorSet _outlineSet;
        std::array<Renderer::GraphicsPipelineID, ModelView::MODEL_TRANSPARENT_BIN_COUNT> _depthPipelines = {};
        std::array<bool, ModelView::MODEL_TRANSPARENT_BIN_COUNT> _activeBins = {};
        Renderer::ComputePipelineID _outlinePipeline = Renderer::ComputePipelineID::Invalid();
        robin_hood::unordered_flat_map<u64, Renderer::BufferID> _bindings;
        u32 _generation = 0;
    };
} // namespace ModelPipeline
