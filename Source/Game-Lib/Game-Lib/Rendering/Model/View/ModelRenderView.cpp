#include "ModelRenderView.h"

#include "Game-Lib/Rendering/Asset/RenderAssetResources.h"
#include "Game-Lib/Rendering/RenderResources.h"
#include "Game-Lib/Rendering/Scene/RenderScene.h"

#include <Renderer/RenderGraph.h>

namespace ModelRendering
{
    ModelRenderView::ModelRenderView(Renderer::Renderer* renderer, GameRenderer* gameRenderer,
                                     RenderAssets::RenderAssetResources* assets,
                                     const RenderScenes::RenderViewDesc& desc, bool validateTransfers)
        : _renderer(renderer), _assets(assets), _view(desc), _state(validateTransfers), _work(renderer),
          _materialResources(renderer, desc.dimensions, desc.dimensionType), _viewWorkPass(renderer, gameRenderer), _visibilityPass(renderer, gameRenderer),
          _materialResolvePass(renderer, gameRenderer), _visibilityResolvePass(renderer, gameRenderer)
    {
    }

    void ModelRenderView::Update(i32 forcedLOD)
    {
        RenderScenes::RenderScene* scene = _view.GetScene();
        if (!scene)
            return;

        if (_state.IsWorkDirty() ||
            _state.GetPreparedSceneRevision() != scene->GetModelInstances().GetMembershipRevision())
        {
            if (_state.GetPreparedSceneRevision() != scene->GetModelInstances().GetMembershipRevision())
                _view.RequestTemporalReset();
            _state.PrepareInputs(*scene, _assets->GetModelGeometryStorage());
            _view.MarkDirty();
        }

        if (_forcedLOD != forcedLOD)
        {
            _forcedLOD = forcedLOD;
            _state.ResetLODHistory();
            _view.RequestTemporalReset();
            _view.RequestMotionReset();
        }
    }

    void ModelRenderView::Upload()
    {
        _state.SyncToGPU(_renderer);
        if (RenderScenes::RenderScene* scene = _view.GetScene())
            _viewWorkPass.PrepareResources(_state, _work, *scene);
    }

    void ModelRenderView::ReportErrors()
    {
        const ModelView::WorkStats& stats = _work.GetStats();
        if (stats.queueOverflows > 0 && !_reportedQueueOverflow)
        {
            NC_LOG_ERROR("MODEL_VIEW queue_overflow view={} dropped={}", _view.GetID(), stats.queueOverflows);
            _reportedQueueOverflow = true;
        }
        else if (stats.queueOverflows == 0)
        {
            _reportedQueueOverflow = false;
        }

        if (stats.visibilityRecordOverflows > 0 && !_reportedVisibilityRecordOverflow)
        {
            u32 committedMeshlets = 0;
            for (u32 count : stats.committedRasterMeshlets)
                committedMeshlets += count;
            NC_LOG_ERROR("MODEL_VIEW visibility_record_overflow view={} dropped={} committed={} capacity={}",
                         _view.GetID(), stats.visibilityRecordOverflows, committedMeshlets, stats.visibilityRecords);
            _reportedVisibilityRecordOverflow = true;
        }
        else if (stats.visibilityRecordOverflows == 0)
        {
            _reportedVisibilityRecordOverflow = false;
        }

        if (stats.visibilityRecordPackingFailures > 0 && !_reportedVisibilityRecordPackingFailure)
        {
            NC_LOG_ERROR("MODEL_VIEW visibility_record_packing_failure view={} dropped={}", _view.GetID(),
                         stats.visibilityRecordPackingFailures);
            _reportedVisibilityRecordPackingFailure = true;
        }
        else if (stats.visibilityRecordPackingFailures == 0)
        {
            _reportedVisibilityRecordPackingFailure = false;
        }

        if (stats.survivorQueueOverflows > 0 && !_reportedSurvivorQueueOverflow)
        {
            NC_LOG_ERROR("MODEL_VIEW survivor_queue_overflow view={} dropped={}", _view.GetID(),
                         stats.survivorQueueOverflows);
            _reportedSurvivorQueueOverflow = true;
        }
        else if (stats.survivorQueueOverflows == 0)
        {
            _reportedSurvivorQueueOverflow = false;
        }
    }

    void ModelRenderView::AddVisibilityPhase1Passes(Renderer::RenderGraph* renderGraph, RenderResources& resources,
                                                     u8 frameIndex)
    {
        _readyThisFrame = false;
        _phase1Ready = false;
        if (!_view.ShouldRender() || !_view.HasPassFamily(RenderScenes::RenderViewPassFamily::Models))
            return;

        RenderScenes::RenderScene* scene = _view.GetScene();
        if (!scene)
            return;

        _work.ReadbackStats(frameIndex);
        _materialResources.ReadbackStats(frameIndex);
        ReportErrors();

        const bool descriptorsReady = _viewWorkPass.Upload(_state, _work, _assets->GetModelGeometryStorage(), *scene);
        _visibilityPass.Upload(_work, _assets->GetModelGeometryStorage(), *scene);
        _visibilityResolvePass.Upload(_work, _assets->GetModelGeometryStorage(), *scene);
        const bool materialDescriptorsReady = _materialResolvePass.Upload(
            _view, _work, _materialResources, _assets->GetModelGeometryStorage(), *scene);
        if (!descriptorsReady || !materialDescriptorsReady)
            return;

        const u32 temporalReset = _view.GetTemporalResetGeneration();
        _viewWorkPass.AddPhase1Pass(renderGraph, resources, _view, _state, _work,
                                    _assets->GetModelGeometryStorage(), _assets->GetMaterialStorage(), *scene,
                                    frameIndex, temporalReset != _handledTemporalReset, _forcedLOD);
        _handledTemporalReset = temporalReset;
        _visibilityPass.AddPass(renderGraph, resources, _view, _work, _assets->GetModelGeometryStorage(),
                                _assets->GetMaterialStorage(), *scene, frameIndex, 1);
        _phase1Ready = true;
    }

    void ModelRenderView::AddVisibilityPhase2Passes(Renderer::RenderGraph* renderGraph, RenderResources& resources,
                                                     u8 frameIndex, bool mainView)
    {
        if (mainView)
        {
            if (!_phase1Ready)
                return;
            RenderScenes::RenderScene& scene = *_view.GetScene();
            _viewWorkPass.AddPhase2Pass(renderGraph, resources, _view, _state, _work,
                                        _assets->GetModelGeometryStorage(), scene, resources.depthPyramid, frameIndex);
            _work.MarkSubmitted(frameIndex);
            _visibilityPass.AddPass(renderGraph, resources, _view, _work, _assets->GetModelGeometryStorage(),
                                    _assets->GetMaterialStorage(), scene, frameIndex, 2);
        }
        else
        {
            _readyThisFrame = false;
            if (!_view.ShouldRender() || !_view.HasPassFamily(RenderScenes::RenderViewPassFamily::Models))
                return;
            RenderScenes::RenderScene* scene = _view.GetScene();
            if (!scene)
                return;
            _work.ReadbackStats(frameIndex);
            _materialResources.ReadbackStats(frameIndex);
            const bool descriptorsReady =
                _viewWorkPass.Upload(_state, _work, _assets->GetModelGeometryStorage(), *scene);
            _visibilityPass.Upload(_work, _assets->GetModelGeometryStorage(), *scene);
            _visibilityResolvePass.Upload(_work, _assets->GetModelGeometryStorage(), *scene);
            const bool materialDescriptorsReady = _materialResolvePass.Upload(
                _view, _work, _materialResources, _assets->GetModelGeometryStorage(), *scene);
            if (!descriptorsReady || !materialDescriptorsReady)
                return;
            const u32 temporalReset = _view.GetTemporalResetGeneration();
            _viewWorkPass.AddPass(renderGraph, resources, _view, _state, _work, _assets->GetModelGeometryStorage(),
                                  _assets->GetMaterialStorage(), *scene, frameIndex,
                                  temporalReset != _handledTemporalReset, _forcedLOD);
            _work.MarkSubmitted(frameIndex);
            _handledTemporalReset = temporalReset;
            _visibilityPass.AddPass(renderGraph, resources, _view, _work, _assets->GetModelGeometryStorage(),
                                    _assets->GetMaterialStorage(), *scene, frameIndex);
        }
        RenderScenes::RenderScene& scene = *_view.GetScene();
        _materialResolvePass.AddClassificationPass(renderGraph, resources, _view, _work, _materialResources,
                                                   _assets->GetModelGeometryStorage(), _assets->GetMaterialStorage(),
                                                   scene, frameIndex);
        _readyThisFrame = true;
    }

    void ModelRenderView::AddPreEffectsPass(Renderer::RenderGraph* renderGraph, RenderResources& resources,
                                             u8 frameIndex)
    {
        if (!_readyThisFrame)
            return;
        RenderScenes::RenderScene& scene = *_view.GetScene();
        _visibilityResolvePass.AddPreEffectsPass(renderGraph, resources, _view, _work,
                                                 _assets->GetModelGeometryStorage(), scene, frameIndex);
    }

    void ModelRenderView::AddMaterialResolvePass(Renderer::RenderGraph* renderGraph, RenderResources& resources,
                                                  u8 frameIndex)
    {
        if (!_readyThisFrame)
            return;
        RenderScenes::RenderScene& scene = *_view.GetScene();
        _materialResolvePass.AddResolvePass(renderGraph, resources, _view, _work, _materialResources,
                                            _assets->GetModelGeometryStorage(), _assets->GetMaterialStorage(), scene,
                                            frameIndex);
        _visibilityResolvePass.AddOpaqueHighlightPass(renderGraph, _view, _work, scene, frameIndex);
        _materialResolvePass.AddRetainedOutputPass(renderGraph, _view);
        _view.MarkRendered();
    }

    void ModelRenderView::AddDiagnosticResolvePass(Renderer::RenderGraph* renderGraph, RenderResources& resources,
                                                    u8 frameIndex)
    {
        if (!_readyThisFrame)
            return;
        RenderScenes::RenderScene& scene = *_view.GetScene();
        _visibilityResolvePass.AddDiagnosticPass(renderGraph, resources, _view, _work,
                                                 _assets->GetModelGeometryStorage(), _assets->GetMaterialStorage(),
                                                 scene, frameIndex);
    }
} // namespace ModelRendering
