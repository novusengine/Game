#pragma once

#include "Game-Lib/Rendering/Material/MaterialResolvePass.h"
#include "Game-Lib/Rendering/Material/MaterialResolveResources.h"
#include "Game-Lib/Rendering/Model/Pipeline/ModelViewWorkPass.h"
#include "Game-Lib/Rendering/Model/Pipeline/ModelVisibilityPass.h"
#include "Game-Lib/Rendering/Model/Pipeline/ModelVisibilityResolvePass.h"
#include "Game-Lib/Rendering/Model/View/ModelViewState.h"
#include "Game-Lib/Rendering/Model/View/ModelViewWorkResources.h"
#include "Game-Lib/Rendering/Scene/RenderView.h"

struct RenderResources;
class GameRenderer;

namespace RenderAssets
{
    class RenderAssetResources;
    struct ModelHandle;
} // namespace RenderAssets

namespace RenderScenes
{
    class RenderScene;
}

namespace Renderer
{
    class DescriptorSet;
    class RenderGraph;
    class RenderGraphBuilder;
    class Renderer;
} // namespace Renderer

namespace ModelRendering
{
    // Coordinates CPU-side model View work and its composed GPU-side passes for the
    // meshlet renderer. It gives GameRenderer one narrow integration point while
    // specialized components retain their own state.
    class ModelRenderSystem
    {
      public:
        ModelRenderSystem(Renderer::Renderer* renderer, GameRenderer* gameRenderer,
                          RenderAssets::RenderAssetResources* assets, RenderScenes::RenderScene* scene,
                          RenderResources& resources, bool validateTransfers = false);

        void Update();
        void Upload();
        void AddVisibilityPasses(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);
        void AddPreEffectsPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);
        void AddMaterialResolvePass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);
        void AddDiagnosticResolvePass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);
        void RegisterPixelQueryResources(Renderer::RenderGraphBuilder& builder) const;
        void BindPixelQueryResources(Renderer::DescriptorSet& descriptorSet);

        // TODO: Remove this development-only selection hook after GPU work expansion
        // replaces diagnostic work.
        RenderScenes::ModelInstanceHandle SetDiagnosticModel(RenderAssets::ModelHandle model,
                                                             const vec3& worldBoundsCenter, f32 worldBoundsRadius,
                                                             bool geometryGroupsEnabled = true);
        const ModelView::WorkStats& GetDiagnosticStats() const
        {
            return _mainViewWork.GetStats();
        }

      private:
        struct PixelQueryBindings
        {
            Renderer::BufferID visibilityRecords0 = Renderer::BufferID::Invalid();
            Renderer::BufferID visibilityRecords1 = Renderer::BufferID::Invalid();
            Renderer::BufferID modelInstances = Renderer::BufferID::Invalid();
        };

        Renderer::Renderer* _renderer = nullptr;
        RenderAssets::RenderAssetResources* _assets = nullptr;
        RenderScenes::RenderScene* _scene = nullptr;
        RenderScenes::RenderView _mainView;
        ModelView::ModelViewState _mainViewState;
        ModelView::ModelViewWorkResources _mainViewWork;
        MaterialRendering::MaterialResolveResources _mainViewMaterialResources;
        ModelPipeline::ModelViewWorkPass _viewWorkPass;
        ModelPipeline::ModelVisibilityPass _visibilityPass;
        MaterialRendering::MaterialResolvePass _materialResolvePass;
        ModelPipeline::ModelVisibilityResolvePass _visibilityResolvePass;
        RenderScenes::ModelInstanceHandle _diagnosticInstance = RenderScenes::InvalidModelInstanceHandle();
        i32 _lastForcedLOD = -1;
        u32 _handledTemporalReset = 0;
        bool _reportedQueueOverflow = false;
        bool _reportedVisibilityRecordOverflow = false;
        bool _reportedVisibilityRecordPackingFailure = false;
        PixelQueryBindings _pixelQueryBindings;
    };
} // namespace ModelRendering
