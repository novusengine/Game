#pragma once

#include <Renderer/DescriptorSet.h>

namespace ModelLoading { class ModelGeometryStorage; }
namespace RenderScenes { class RenderScene; }
namespace Renderer { class RenderGraphBuilder; }

namespace ModelPipeline
{
    // Shared tracked bindings and render-graph usage for the geometry surface consumed by model raster passes.
    class ModelGeometryBindings
    {
      public:
        bool Bind(Renderer::DescriptorSet& descriptorSet, const ModelLoading::ModelGeometryStorage& geometry, const RenderScenes::RenderScene& scene);
        static void RegisterUsage(Renderer::RenderGraphBuilder& builder, const ModelLoading::ModelGeometryStorage& geometry, const RenderScenes::RenderScene& scene, Renderer::BufferPassUsage usage);

      private:
        bool BindOne(Renderer::DescriptorSet& descriptorSet, StringUtils::StringHash name, Renderer::BufferID buffer, Renderer::BufferID& current);

        Renderer::BufferID _instances = Renderer::BufferID::Invalid();
        Renderer::BufferID _models = Renderer::BufferID::Invalid();
        Renderer::BufferID _meshes = Renderer::BufferID::Invalid();
        Renderer::BufferID _lods = Renderer::BufferID::Invalid();
        Renderer::BufferID _submeshes = Renderer::BufferID::Invalid();
        Renderer::BufferID _meshlets = Renderer::BufferID::Invalid();
        Renderer::BufferID _positions = Renderer::BufferID::Invalid();
        Renderer::BufferID _vertexAttributes = Renderer::BufferID::Invalid();
        Renderer::BufferID _vertexIndices = Renderer::BufferID::Invalid();
        Renderer::BufferID _triangles = Renderer::BufferID::Invalid();
        Renderer::BufferID _materialTable = Renderer::BufferID::Invalid();
    };
}
