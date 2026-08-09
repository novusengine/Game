#pragma once

#include "Game-Lib/Rendering/Material/MaterialResolveResources.h"
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
    // Owns GPU-side tile classification and grouped material-evaluation pipelines for visible surfaces.
    // Classification bounds shader divergence while indirect group dispatches avoid resolving empty screen tiles.
    class MaterialResolvePass
    {
      public:
        MaterialResolvePass(Renderer::Renderer* renderer, GameRenderer* gameRenderer);

        bool Upload(const RenderScenes::RenderView& view, const ModelView::ModelViewWorkResources& work,
                    MaterialResolveResources& resources,
                    const ModelLoading::ModelGeometryStorage& geometry,
                    const RenderScenes::RenderScene& scene);
        void AddClassificationPass(Renderer::RenderGraph* renderGraph, RenderResources& renderResources,
                                   const RenderScenes::RenderView& view,
                                   const ModelView::ModelViewWorkResources& work,
                                   MaterialResolveResources& resources,
                                   const ModelLoading::ModelGeometryStorage& geometry,
                                   const MaterialLoading::MaterialStorage& materials,
                                   const RenderScenes::RenderScene& scene, u8 frameIndex);
        void AddResolvePass(Renderer::RenderGraph* renderGraph, RenderResources& renderResources,
                            const RenderScenes::RenderView& view,
                            const ModelView::ModelViewWorkResources& work,
                            MaterialResolveResources& resources,
                            const ModelLoading::ModelGeometryStorage& geometry,
                            const MaterialLoading::MaterialStorage& materials,
                            const RenderScenes::RenderScene& scene, u8 frameIndex);

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

        struct ClassificationBindings
        {
            ModelBindings model;
            Renderer::BufferID tileQueues[ModelView::MODEL_VIEW_FRAME_COUNT] = {};
            Renderer::BufferID counters[ModelView::MODEL_VIEW_FRAME_COUNT] = {};
        };

        struct ResolveBindings
        {
            ModelBindings model;
            Renderer::BufferID tileQueues[ModelView::MODEL_VIEW_FRAME_COUNT] = {};
        };

        struct FinalizeBindings
        {
            Renderer::BufferID counters[ModelView::MODEL_VIEW_FRAME_COUNT] = {};
            Renderer::BufferID arguments[ModelView::MODEL_VIEW_FRAME_COUNT] = {};
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

        Renderer::Renderer* _renderer = nullptr;
        Renderer::DescriptorSet _classificationSet;
        Renderer::DescriptorSet _finalizeSet;
        Renderer::DescriptorSet _resolveSet;
        Renderer::ComputePipelineID _classificationPipeline = Renderer::ComputePipelineID::Invalid();
        Renderer::ComputePipelineID _finalizePipeline = Renderer::ComputePipelineID::Invalid();
        std::array<Renderer::ComputePipelineID, FileFormat::Material::ABI::EXECUTION_GROUP_COUNT>
            _resolvePipelines = {};
        std::array<bool, FileFormat::Material::ABI::EXECUTION_GROUP_COUNT> _activeResolveGroups = {};
        ClassificationBindings _classificationBindings;
        FinalizeBindings _finalizeBindings;
        ResolveBindings _resolveBindings;
        u32 _resourceGeneration = 0;
        u32 _descriptorWarmupFrames = 0;
    };
} // namespace MaterialRendering
