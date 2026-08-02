#pragma once

#include "Game-Lib/Rendering/Model/Pipeline/ModelDiagnosticPass.h"
#include "Game-Lib/Rendering/Model/Pipeline/ModelViewWorkPass.h"
#include "Game-Lib/Rendering/Model/View/ModelViewState.h"
#include "Game-Lib/Rendering/Model/View/ModelViewWorkResources.h"
#include "Game-Lib/Rendering/Scene/RenderView.h"

struct RenderResources;
class GameRenderer;

namespace RenderAssets
{
    class RenderAssetResources;
    struct ModelHandle;
}

namespace RenderScenes
{
    class RenderScene;
}

namespace Renderer
{
    class RenderGraph;
    class Renderer;
}

namespace ModelRendering
{
    // Coordinates CPU-side model View work and its composed GPU-side passes for the meshlet renderer.
    // It gives GameRenderer one narrow integration point while specialized components retain their own state.
    class ModelRenderSystem
    {
      public:
        ModelRenderSystem(Renderer::Renderer* renderer, GameRenderer* gameRenderer,
                          RenderAssets::RenderAssetResources* assets, RenderScenes::RenderScene* scene,
                          RenderResources& resources, bool validateTransfers = false);

        void Update();
        void Upload();
        void AddPasses(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);

        // TODO: Remove this development-only selection hook after GPU work expansion replaces diagnostic work.
        RenderScenes::ModelInstanceHandle SetDiagnosticModel(RenderAssets::ModelHandle model,
                                                             const vec3& worldBoundsCenter, f32 worldBoundsRadius);
        const ModelView::WorkStats& GetDiagnosticStats() const
        {
            return _mainViewWork.GetStats();
        }

      private:
        Renderer::Renderer* _renderer = nullptr;
        RenderAssets::RenderAssetResources* _assets = nullptr;
        RenderScenes::RenderScene* _scene = nullptr;
        RenderScenes::RenderView _mainView;
        ModelView::ModelViewState _mainViewState;
        ModelView::ModelViewWorkResources _mainViewWork;
        ModelPipeline::ModelViewWorkPass _viewWorkPass;
        ModelPipeline::ModelDiagnosticPass _diagnosticPass;
        RenderScenes::ModelInstanceHandle _diagnosticInstance = RenderScenes::InvalidModelInstanceHandle();
        i32 _lastForcedLOD = -1;
        u32 _handledTemporalReset = 0;
        bool _reportedQueueOverflow = false;
    };
} // namespace ModelRendering
