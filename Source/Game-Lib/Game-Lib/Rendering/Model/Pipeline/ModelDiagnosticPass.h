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
    // Owns the GPU-side pipelines and descriptor bindings for CPU-authored diagnostic meshlet work.
    // It renders known work directly so geometry decoding can be verified before production culling is introduced.
    // TODO: Remove this temporary bring-up pass after the production visibility and material path replaces it.
    class ModelDiagnosticPass
    {
      public:
        ModelDiagnosticPass(Renderer::Renderer* renderer, GameRenderer* gameRenderer);

        void Upload(ModelView::ModelViewState& viewState, const ModelLoading::ModelGeometryStorage& geometry,
                    const RenderScenes::RenderScene& scene);
        void AddPass(Renderer::RenderGraph* renderGraph, RenderResources& resources,
                     const RenderScenes::RenderView& view, const ModelView::ModelViewState& viewState,
                     const ModelLoading::ModelGeometryStorage& geometry, const RenderScenes::RenderScene& scene,
                     u8 frameIndex);

      private:
        void BindIfChanged(StringUtils::StringHash name, Renderer::BufferID buffer, Renderer::BufferID& current);

        Renderer::Renderer* _renderer = nullptr;
        Renderer::DescriptorSet _descriptorSet;
        Renderer::GraphicsPipelineID _oneSidedPipeline = Renderer::GraphicsPipelineID::Invalid();
        Renderer::GraphicsPipelineID _twoSidedPipeline = Renderer::GraphicsPipelineID::Invalid();

        Renderer::BufferID _workBuffer = Renderer::BufferID::Invalid();
        Renderer::BufferID _instanceBuffer = Renderer::BufferID::Invalid();
        Renderer::BufferID _meshletBuffer = Renderer::BufferID::Invalid();
        Renderer::BufferID _positionBuffer = Renderer::BufferID::Invalid();
        Renderer::BufferID _vertexAttributeBuffer = Renderer::BufferID::Invalid();
        Renderer::BufferID _meshletVertexIndexBuffer = Renderer::BufferID::Invalid();
        Renderer::BufferID _meshletTriangleBuffer = Renderer::BufferID::Invalid();
    };
} // namespace ModelPipeline
