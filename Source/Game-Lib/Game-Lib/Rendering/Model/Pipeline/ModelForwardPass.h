#pragma once

#include "Game-Lib/Rendering/Model/View/ModelViewWork.h"

#include <Renderer/DescriptorSet.h>
#include <Renderer/Descriptors/GraphicsPipelineDesc.h>

struct RenderResources;
class GameRenderer;

namespace MaterialLoading { class MaterialStorage; }
namespace ModelLoading { class ModelGeometryStorage; }
namespace ModelView { class ModelViewWorkResources; }
namespace RenderScenes { class RenderScene; class RenderView; }
namespace Renderer { class RenderGraph; class Renderer; }

namespace ModelPipeline
{
    // Owns GPU-side pipelines and bindings that shade opaque and alpha-tested model meshlets directly into a View color target.
    // Direct forward rasterization lets small Views reuse authored Materials without allocating visibility-resolve resources.
    class ModelForwardPass
    {
      public:
        ModelForwardPass(Renderer::Renderer* renderer, GameRenderer* gameRenderer);

        bool Upload(const ModelView::ModelViewWorkResources& work,
                    const ModelLoading::ModelGeometryStorage& geometry,
                    const RenderScenes::RenderScene& scene);
        void AddPass(Renderer::RenderGraph* renderGraph, RenderResources& resources,
                     const RenderScenes::RenderView& view, const ModelView::ModelViewWorkResources& work,
                     const ModelLoading::ModelGeometryStorage& geometry,
                     const MaterialLoading::MaterialStorage& materials, const RenderScenes::RenderScene& scene,
                     u8 frameIndex);

      private:
        bool Bind(StringUtils::StringHash name, Renderer::BufferID buffer, Renderer::BufferID& current);

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

        Renderer::Renderer* _renderer = nullptr;
        GameRenderer* _gameRenderer = nullptr;
        Renderer::DescriptorSet _descriptorSet;
        Renderer::GraphicsPipelineID _oneSidedPipeline = Renderer::GraphicsPipelineID::Invalid();
        Renderer::GraphicsPipelineID _twoSidedPipeline = Renderer::GraphicsPipelineID::Invalid();
        Bindings _bindings;
        u32 _queueGeneration = 0;
        u32 _descriptorWarmupFrames = 0;
    };
} // namespace ModelPipeline
