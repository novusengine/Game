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

namespace RenderAssets { class RenderAssetResources; }
namespace Renderer { class RenderGraph; class Renderer; }

namespace ModelRendering
{
    // Owns the CPU- and GPU-side model state and descriptor bindings for one RenderView.
    // Per-View ownership keeps queued work and bindings independent when several Views share global assets.
    class ModelRenderView
    {
      public:
        ModelRenderView(Renderer::Renderer* renderer, GameRenderer* gameRenderer,
                        RenderAssets::RenderAssetResources* assets, const RenderScenes::RenderViewDesc& desc,
                        bool validateTransfers = false);

        void Update(i32 forcedLOD);
        void Upload();
        void AddVisibilityPhase1Passes(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);
        void AddVisibilityPhase2Passes(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex,
                                       bool mainView);
        void AddPreEffectsPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);
        void AddMaterialResolvePass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);
        void AddDiagnosticResolvePass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);

        RenderScenes::RenderView& GetView() { return _view; }
        const RenderScenes::RenderView& GetView() const { return _view; }
        const ModelView::ModelViewWorkResources& GetWork() const { return _work; }
        ModelView::ModelViewState& GetState() { return _state; }

      private:
        void ReportErrors();

        Renderer::Renderer* _renderer = nullptr;
        RenderAssets::RenderAssetResources* _assets = nullptr;
        RenderScenes::RenderView _view;
        ModelView::ModelViewState _state;
        ModelView::ModelViewWorkResources _work;
        MaterialRendering::MaterialResolveResources _materialResources;
        ModelPipeline::ModelViewWorkPass _viewWorkPass;
        ModelPipeline::ModelVisibilityPass _visibilityPass;
        MaterialRendering::MaterialResolvePass _materialResolvePass;
        ModelPipeline::ModelVisibilityResolvePass _visibilityResolvePass;
        i32 _forcedLOD = -1;
        u32 _handledTemporalReset = 0;
        bool _readyThisFrame = false;
        bool _phase1Ready = false;
        bool _reportedQueueOverflow = false;
        bool _reportedVisibilityRecordOverflow = false;
        bool _reportedVisibilityRecordPackingFailure = false;
        bool _reportedSurvivorQueueOverflow = false;
    };
} // namespace ModelRendering
