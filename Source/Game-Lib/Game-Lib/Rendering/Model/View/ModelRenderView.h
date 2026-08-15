#pragma once

#include "Game-Lib/Rendering/Material/MaterialResolvePass.h"
#include "Game-Lib/Rendering/Material/MaterialResolveResources.h"
#include "Game-Lib/Rendering/Model/Pipeline/ModelForwardPass.h"
#include "Game-Lib/Rendering/Model/Pipeline/ModelViewWorkPass.h"
#include "Game-Lib/Rendering/Model/Pipeline/ModelTransparentPass.h"
#include "Game-Lib/Rendering/Model/Pipeline/ModelTransparentSelectionPass.h"
#include "Game-Lib/Rendering/Model/Pipeline/ModelVisibilityPass.h"
#include "Game-Lib/Rendering/Model/Pipeline/ModelVisibilityResolvePass.h"
#include "Game-Lib/Rendering/Model/View/ModelViewState.h"
#include "Game-Lib/Rendering/Model/View/ModelViewWorkResources.h"
#include "Game-Lib/Rendering/Model/View/ModelTransparentWorkResources.h"
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
        ~ModelRenderView();

        void Update(i32 forcedLOD);
        void Upload();
        void AddVisibilityPhase1Passes(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);
        void AddVisibilityPhase2Passes(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex,
                                       bool mainView);
        void AddPreEffectsPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);
        void AddMaterialResolvePass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);
        void AddTransparentCullPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);
        void AddTransparentRasterPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);
        void AddTransparentSelectionOutlinePass(Renderer::RenderGraph* renderGraph, RenderResources& resources,
                                                u8 frameIndex);
        void AddRetainedOutputPass(Renderer::RenderGraph* renderGraph);
        void AddDiagnosticResolvePass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);

        RenderScenes::RenderView& GetView() { return _view; }
        const RenderScenes::RenderView& GetView() const { return _view; }
        bool IsReadyThisFrame() const { return _readyThisFrame; }
        const ModelView::ModelViewWorkResources& GetWork() const { return _work; }
        const ModelView::ModelTransparentWorkResources& GetTransparentWork() const { return _transparentWork; }
        ModelView::ModelViewState& GetState() { return _state; }

      private:
        void ReportErrors();
        void UpdateSelectionTarget();

        Renderer::Renderer* _renderer = nullptr;
        RenderAssets::RenderAssetResources* _assets = nullptr;
        RenderScenes::RenderView _view;
        ModelView::ModelViewState _state;
        ModelView::ModelViewWorkResources _work;
        ModelView::ModelTransparentWorkResources _transparentWork;
        MaterialRendering::MaterialResolveResources _materialResources;
        ModelPipeline::ModelViewWorkPass _viewWorkPass;
        ModelPipeline::ModelVisibilityPass _visibilityPass;
        ModelPipeline::ModelForwardPass _forwardPass;
        ModelPipeline::ModelTransparentPass _transparentPass;
        ModelPipeline::ModelTransparentSelectionPass _transparentSelectionPass;
        MaterialRendering::MaterialResolvePass _materialResolvePass;
        ModelPipeline::ModelVisibilityResolvePass _visibilityResolvePass;
        i32 _forcedLOD = -1;
        u32 _handledTemporalReset = 0;
        bool _readyThisFrame = false;
        bool _phase1Ready = false;
        bool _transparentDescriptorsReady = false;
        bool _transparentSelectionDescriptorsReady = false;
        Renderer::DepthImageID _transparentSelectionDepth = Renderer::DepthImageID::Invalid();
        uvec2 _transparentSelectionDimensions = {};
        bool _reportedQueueOverflow = false;
        bool _reportedVisibilityRecordOverflow = false;
        bool _reportedVisibilityRecordPackingFailure = false;
        bool _reportedSurvivorQueueOverflow = false;
    };
} // namespace ModelRendering
