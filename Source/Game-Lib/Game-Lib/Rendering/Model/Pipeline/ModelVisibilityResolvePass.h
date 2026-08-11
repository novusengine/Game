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

namespace ModelPipeline
{
    // Owns GPU-side model visibility reconstruction pipelines and their frame-local record bindings.
    // Reconstruction supplies geometric normals and human-readable ID views from the same typed pixels.
    class ModelVisibilityResolvePass
    {
      public:
        ModelVisibilityResolvePass(Renderer::Renderer* renderer, GameRenderer* gameRenderer);

        void Upload(const ModelView::ModelViewWorkResources& work, const ModelLoading::ModelGeometryStorage& geometry,
                    const RenderScenes::RenderScene& scene);
        void AddPreEffectsPass(Renderer::RenderGraph* renderGraph, RenderResources& resources,
                               const RenderScenes::RenderView& view, const ModelView::ModelViewWorkResources& work,
                               const ModelLoading::ModelGeometryStorage& geometry,
                               const RenderScenes::RenderScene& scene, u8 frameIndex);
        void AddOpaqueHighlightPass(Renderer::RenderGraph* renderGraph, const RenderScenes::RenderView& view,
                                    const ModelView::ModelViewWorkResources& work,
                                    const RenderScenes::RenderScene& scene, u8 frameIndex);
        // TODO: Remove the diagnostic resolve after production material shading consumes typed visibility.
        void AddDiagnosticPass(Renderer::RenderGraph* renderGraph, RenderResources& resources,
                               const RenderScenes::RenderView& view, const ModelView::ModelViewWorkResources& work,
                               const ModelLoading::ModelGeometryStorage& geometry,
                               const MaterialLoading::MaterialStorage& materials,
                               const RenderScenes::RenderScene& scene, u8 frameIndex);

      private:
        struct FrameBindings
        {
            Renderer::BufferID visibilityRecords = Renderer::BufferID::Invalid();
            Renderer::BufferID workStats = Renderer::BufferID::Invalid();
        };

        struct CommonBindings
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
        };

        struct DiagnosticBindings
        {
            CommonBindings common;
            Renderer::BufferID materialTable = Renderer::BufferID::Invalid();
            Renderer::BufferID cullReasons[ModelView::MODEL_VIEW_FRAME_COUNT] = {};
        };

        struct HighlightBindings
        {
            FrameBindings frames[ModelView::MODEL_VIEW_FRAME_COUNT];
            Renderer::BufferID modelInstances = Renderer::BufferID::Invalid();
        };

        void BindCommon(Renderer::DescriptorSet& set, CommonBindings& bindings,
                        const ModelView::ModelViewWorkResources& work,
                        const ModelLoading::ModelGeometryStorage& geometry,
                        const RenderScenes::RenderScene& scene);
        void RegisterCommonUsage(Renderer::RenderGraphBuilder& builder,
                                 const ModelView::ModelViewWorkResources& work,
                                 const ModelLoading::ModelGeometryStorage& geometry,
                                 const RenderScenes::RenderScene& scene) const;

        Renderer::Renderer* _renderer = nullptr;
        Renderer::DescriptorSet _preEffectsSet;
        Renderer::DescriptorSet _velocitySet;
        Renderer::DescriptorSet _highlightSet;
        Renderer::DescriptorSet _diagnosticSet;
        Renderer::ComputePipelineID _preEffectsPipeline = Renderer::ComputePipelineID::Invalid();
        Renderer::ComputePipelineID _velocityPipeline = Renderer::ComputePipelineID::Invalid();
        Renderer::ComputePipelineID _highlightPipeline = Renderer::ComputePipelineID::Invalid();
        Renderer::ComputePipelineID _diagnosticPipeline = Renderer::ComputePipelineID::Invalid();
        CommonBindings _preEffectsBindings;
        CommonBindings _velocityBindings;
        HighlightBindings _highlightBindings;
        DiagnosticBindings _diagnosticBindings;
    };
} // namespace ModelPipeline
