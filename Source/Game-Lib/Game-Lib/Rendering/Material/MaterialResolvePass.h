#pragma once

#include "Game-Lib/Rendering/Model/View/ModelViewWork.h"

#include <Renderer/DescriptorSet.h>
#include <Renderer/Descriptors/ComputePipelineDesc.h>

struct RenderResources;
class GameRenderer;

namespace MaterialLoading { class MaterialStorage; }
namespace ModelLoading { class ModelGeometryStorage; }
namespace ModelView { class ModelViewWorkResources; }
namespace RenderScenes { class RenderScene; class RenderView; }
namespace Renderer { class RenderGraph; class RenderGraphBuilder; class Renderer; }

namespace MaterialRendering
{
    // Resolves model visibility pixels in one full-screen dispatch with authored execution-group evaluation.
    class MaterialResolvePass
    {
      public:
        MaterialResolvePass(Renderer::Renderer* renderer, GameRenderer* gameRenderer);

        bool Upload(const ModelView::ModelViewWorkResources& work, const ModelLoading::ModelGeometryStorage& geometry,
                    const RenderScenes::RenderScene& scene);
        void AddResolvePass(Renderer::RenderGraph* renderGraph, RenderResources& renderResources,
                            const RenderScenes::RenderView& view,
                            const ModelView::ModelViewWorkResources& work,
                            const ModelLoading::ModelGeometryStorage& geometry,
                            const MaterialLoading::MaterialStorage& materials,
                            const RenderScenes::RenderScene& scene, u8 frameIndex);
        void AddRetainedOutputPass(Renderer::RenderGraph* renderGraph,
                                   const RenderScenes::RenderView& view);

      private:
        struct ModelBindings
        {
            Renderer::BufferID visibilityRecords[ModelView::MODEL_VIEW_FRAME_COUNT] = {};
            Renderer::BufferID stats[ModelView::MODEL_VIEW_FRAME_COUNT] = {};
            Renderer::BufferID instances = Renderer::BufferID::Invalid();
            Renderer::BufferID models = Renderer::BufferID::Invalid();
            Renderer::BufferID meshes = Renderer::BufferID::Invalid();
            Renderer::BufferID lods = Renderer::BufferID::Invalid();
            Renderer::BufferID submeshes = Renderer::BufferID::Invalid();
            Renderer::BufferID meshlets = Renderer::BufferID::Invalid();
            Renderer::BufferID positions = Renderer::BufferID::Invalid();
            Renderer::BufferID attributes = Renderer::BufferID::Invalid();
            Renderer::BufferID indices = Renderer::BufferID::Invalid();
            Renderer::BufferID triangles = Renderer::BufferID::Invalid();
            Renderer::BufferID materialTable = Renderer::BufferID::Invalid();
        };

        struct ResolveBindings
        {
            ModelBindings model;
        };

        void BindModelResources(Renderer::DescriptorSet& set, ModelBindings& bindings,
                                const ModelView::ModelViewWorkResources& work,
                                const ModelLoading::ModelGeometryStorage& geometry,
                                const RenderScenes::RenderScene& scene, bool& changed);
        void RegisterModelUsage(Renderer::RenderGraphBuilder& builder,
                                const ModelView::ModelViewWorkResources& work,
                                const ModelLoading::ModelGeometryStorage& geometry,
                                const MaterialLoading::MaterialStorage& materials,
                                const RenderScenes::RenderScene& scene) const;

        GameRenderer* _gameRenderer = nullptr;
        Renderer::DescriptorSet _resolveSet;
        Renderer::ComputePipelineID _resolvePipeline = Renderer::ComputePipelineID::Invalid();
        ResolveBindings _resolveBindings;
    };
} // namespace MaterialRendering
