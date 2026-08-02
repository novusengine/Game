#pragma once

#include <Renderer/DescriptorSet.h>
#include <Renderer/Descriptors/GraphicsPipelineDesc.h>

struct RenderResources;
class GameRenderer;

namespace ModelLoading
{
    class ModelGeometryStorage;
}

namespace ModelView
{
    class ModelViewState;
    class ModelViewWorkResources;
}

namespace RenderScenes
{
    class RenderScene;
    class RenderView;
}

namespace Renderer
{
    class RenderGraph;
    class Renderer;
}

namespace ModelPipeline
{
    // Owns the GPU-side pipelines and descriptor bindings for diagnostic model meshlet rasterization.
    // It consumes GPU-culled indirect queues so visibility and geometry decoding can be inspected together.
    // TODO: Remove this temporary bring-up pass after the production visibility and material path replaces it.
    class ModelDiagnosticPass
    {
      public:
        ModelDiagnosticPass(Renderer::Renderer* renderer, GameRenderer* gameRenderer);

        void Upload(const ModelView::ModelViewWorkResources& work, const ModelLoading::ModelGeometryStorage& geometry,
                    const RenderScenes::RenderScene& scene);
        void AddPass(Renderer::RenderGraph* renderGraph, RenderResources& resources,
                     const RenderScenes::RenderView& view, const ModelView::ModelViewWorkResources& work,
                     const ModelLoading::ModelGeometryStorage& geometry, const RenderScenes::RenderScene& scene,
                     u8 frameIndex);

      private:
        void BindIfChanged(StringUtils::StringHash name, Renderer::BufferID buffer, Renderer::BufferID& current);

        Renderer::Renderer* _renderer = nullptr;
        Renderer::DescriptorSet _descriptorSet;
        Renderer::GraphicsPipelineID _oneSidedPipeline = Renderer::GraphicsPipelineID::Invalid();
        Renderer::GraphicsPipelineID _twoSidedPipeline = Renderer::GraphicsPipelineID::Invalid();

        Renderer::BufferID _oneSidedWorkBuffers[2] = {};
        Renderer::BufferID _twoSidedWorkBuffers[2] = {};
        Renderer::BufferID _statsBuffers[2] = {};
        Renderer::BufferID _instanceBuffer = Renderer::BufferID::Invalid();
        Renderer::BufferID _meshletBuffer = Renderer::BufferID::Invalid();
        Renderer::BufferID _positionBuffer = Renderer::BufferID::Invalid();
        Renderer::BufferID _vertexAttributeBuffer = Renderer::BufferID::Invalid();
        Renderer::BufferID _meshletVertexIndexBuffer = Renderer::BufferID::Invalid();
        Renderer::BufferID _meshletTriangleBuffer = Renderer::BufferID::Invalid();
        u32 _queueGeneration = 0;
    };
} // namespace ModelPipeline
