#pragma once

#include "Game-Lib/Rendering/Model/View/ModelViewWork.h"

#include <Renderer/DescriptorSet.h>
#include <Renderer/Descriptors/GraphicsPipelineDesc.h>

struct RenderResources;
class GameRenderer;

namespace ModelLoading { class ModelGeometryStorage; }
namespace MaterialLoading { class MaterialStorage; }
namespace ModelView { class ModelViewWorkResources; }
namespace RenderScenes { class RenderScene; class RenderView; }
namespace Renderer { class RenderGraph; class Renderer; }

namespace ModelPipeline
{
    // Owns GPU-side pipelines and bindings that rasterize model meshlets into the shared visibility target.
    // The pass turns frame-local record IDs into typed pixels that later consumers can safely reconstruct.
    class ModelVisibilityPass
    {
      public:
        ModelVisibilityPass(Renderer::Renderer* renderer, GameRenderer* gameRenderer);

        void Upload(const ModelView::ModelViewWorkResources& work,
                    const ModelLoading::ModelGeometryStorage& geometry,
                    const RenderScenes::RenderScene& scene);
        void AddPass(Renderer::RenderGraph* renderGraph, RenderResources& resources,
                     const RenderScenes::RenderView& view, const ModelView::ModelViewWorkResources& work,
                     const ModelLoading::ModelGeometryStorage& geometry,
                     const MaterialLoading::MaterialStorage& materials, const RenderScenes::RenderScene& scene,
                     u8 frameIndex);

      private:
        struct FrameBindings
        {
            Renderer::BufferID rasterQueues[ModelView::MODEL_RASTER_CLASS_COUNT] = {};
            Renderer::BufferID visibilityRecords = Renderer::BufferID::Invalid();
            Renderer::BufferID workStats = Renderer::BufferID::Invalid();
        };

        struct Bindings
        {
            FrameBindings frames[ModelView::MODEL_VIEW_FRAME_COUNT];
            Renderer::BufferID modelInstances = Renderer::BufferID::Invalid();
            Renderer::BufferID models = Renderer::BufferID::Invalid();
            Renderer::BufferID meshes = Renderer::BufferID::Invalid();
            Renderer::BufferID lods = Renderer::BufferID::Invalid();
            Renderer::BufferID submeshes = Renderer::BufferID::Invalid();
            Renderer::BufferID meshlets = Renderer::BufferID::Invalid();
            Renderer::BufferID positions = Renderer::BufferID::Invalid();
            Renderer::BufferID vertexAttributes = Renderer::BufferID::Invalid();
            Renderer::BufferID vertexIndices = Renderer::BufferID::Invalid();
            Renderer::BufferID triangles = Renderer::BufferID::Invalid();
            Renderer::BufferID materialTable = Renderer::BufferID::Invalid();
        };

        void BindIfChanged(StringUtils::StringHash name, Renderer::BufferID buffer, Renderer::BufferID& current);

        Renderer::Renderer* _renderer = nullptr;
        Renderer::DescriptorSet _descriptorSet;
        Renderer::GraphicsPipelineID _oneSidedPipeline = Renderer::GraphicsPipelineID::Invalid();
        Renderer::GraphicsPipelineID _twoSidedPipeline = Renderer::GraphicsPipelineID::Invalid();
        Renderer::GraphicsPipelineID _alphaTestOneSidedPipeline = Renderer::GraphicsPipelineID::Invalid();
        Renderer::GraphicsPipelineID _alphaTestTwoSidedPipeline = Renderer::GraphicsPipelineID::Invalid();
        Bindings _bindings;
        u32 _queueGeneration = 0;
    };
} // namespace ModelPipeline
