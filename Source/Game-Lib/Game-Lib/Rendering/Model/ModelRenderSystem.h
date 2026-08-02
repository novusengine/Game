#pragma once

#include "Game-Lib/Rendering/Model/Pipeline/ModelDiagnosticPass.h"
#include "Game-Lib/Rendering/Model/View/ModelViewState.h"
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
        const ModelView::DiagnosticWorkStats& GetDiagnosticStats() const
        {
            return _mainViewState.GetDiagnosticStats();
        }

      private:
        Renderer::Renderer* _renderer = nullptr;
        RenderAssets::RenderAssetResources* _assets = nullptr;
        RenderScenes::RenderScene* _scene = nullptr;
        RenderScenes::RenderView _mainView;
        ModelView::ModelViewState _mainViewState;
        ModelPipeline::ModelDiagnosticPass _diagnosticPass;
        RenderScenes::ModelInstanceHandle _diagnosticInstance = RenderScenes::InvalidModelInstanceHandle();
    };
} // namespace ModelRendering
