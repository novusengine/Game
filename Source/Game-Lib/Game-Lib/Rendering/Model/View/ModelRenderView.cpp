#include "ModelRenderView.h"

#include "Game-Lib/Rendering/Asset/RenderAssetResources.h"
#include "Game-Lib/Rendering/CullUtils.h"
#include "Game-Lib/Rendering/RenderResources.h"
#include "Game-Lib/Rendering/Scene/RenderScene.h"

#include <Renderer/RenderGraph.h>
#include <Renderer/Renderer.h>

namespace ModelRendering
{
    ModelRenderView::ModelRenderView(Renderer::Renderer* renderer, GameRenderer* gameRenderer,
                                     RenderAssets::RenderAssetResources* assets,
                                     const RenderScenes::RenderViewDesc& desc, bool validateTransfers)
        : _renderer(renderer), _assets(assets), _view(desc), _state(validateTransfers), _work(renderer),
          _transparentWork(renderer), _viewWorkPass(renderer, gameRenderer),
          _transparentPass(renderer, gameRenderer), _transparentSelectionPass(renderer, gameRenderer)
    {
        if (_view.HasPassFamily(RenderScenes::RenderViewPassFamily::Models))
        {
            _materialResources = std::make_unique<MaterialRendering::MaterialResolveResources>(renderer, desc.dimensions, desc.dimensionType);
            _visibilityPass = std::make_unique<ModelPipeline::ModelVisibilityPass>(renderer, gameRenderer);
            _materialResolvePass = std::make_unique<MaterialRendering::MaterialResolvePass>(renderer, gameRenderer);
            _visibilityResolvePass = std::make_unique<ModelPipeline::ModelVisibilityResolvePass>(renderer, gameRenderer);
        }
        else
        {
            _forwardPass = std::make_unique<ModelPipeline::ModelForwardPass>(renderer, gameRenderer);
        }
    }

    ModelRenderView::~ModelRenderView()
    {
        if (_transparentSelectionDepth != Renderer::DepthImageID::Invalid())
            _renderer->DestroyDepthImage(_transparentSelectionDepth);
    }

    void ModelRenderView::UpdateSelectionTarget()
    {
        RenderScenes::RenderScene* scene = _view.GetScene();
        const bool active = scene && scene->HasTransparentModelHighlights();
        const bool dimensionsChanged = _view.GetDimensionType() == Renderer::ImageDimensionType::DIMENSION_ABSOLUTE &&
            _transparentSelectionDimensions != _view.GetDimensions();
        if ((!active || dimensionsChanged) && _transparentSelectionDepth != Renderer::DepthImageID::Invalid())
        {
            _renderer->DestroyDepthImage(_transparentSelectionDepth);
            _transparentSelectionDepth = Renderer::DepthImageID::Invalid();
            _transparentSelectionDescriptorsReady = false;
        }
        if (!active || _transparentSelectionDepth != Renderer::DepthImageID::Invalid())
            return;

        Renderer::DepthImageDesc desc;
        desc.debugName = _view.GetDebugName() + " Transparent Selection Depth";
        desc.dimensions = _view.GetDimensionType() == Renderer::ImageDimensionType::DIMENSION_ABSOLUTE ? vec2(_view.GetDimensions()) : vec2(1.0f);
        desc.dimensionType = _view.GetDimensionType();
        desc.format = Renderer::DepthImageFormat::D32_FLOAT;
        desc.depthClearValue = 0.0f;
        _transparentSelectionDepth = _renderer->CreateDepthImage(desc);
        _transparentSelectionDimensions = _view.GetDimensions();
    }

    void ModelRenderView::Update(i32 forcedLOD)
    {
        RenderScenes::RenderScene* scene = _view.GetScene();
        if (!scene)
            return;

        UpdateSelectionTarget();

        const bool sceneMembershipChanged = _state.GetPreparedSceneRevision() != scene->GetModelInstances().GetMembershipRevision();
        if (_state.IsWorkDirty() ||
            (sceneMembershipChanged && !_state.GetDiagnosticSelection().empty()))
        {
            _state.PrepareInputs(*scene, _assets->GetModelGeometryStorage());
            _state.PrepareTransparentStats(*scene, _assets->GetModelGeometryStorage(), _assets->GetMaterialStorage());
            _view.MarkDirty();
        }
        else if (sceneMembershipChanged)
        {
            _state.PrepareChangedInputs(*scene, _assets->GetModelGeometryStorage(), scene->GetModelMembershipChanges());
            _state.PrepareChangedTransparentStats(*scene, _assets->GetModelGeometryStorage(), _assets->GetMaterialStorage(), scene->GetModelMembershipChanges());
            _view.MarkDirty();
        }

        if (_state.GetPreparedTransparentRoutingRevision() != scene->GetTransparentRoutingRevision())
            _state.PrepareChangedTransparentStats(*scene, _assets->GetModelGeometryStorage(), _assets->GetMaterialStorage(), scene->GetTransparentRoutingChanges());

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
        {
            _viewWorkPass.PrepareResources(_state, _work, *scene);
            _viewWorkDescriptorsReady = _viewWorkPass.Upload(_state, _work, _assets->GetModelGeometryStorage(), *scene);
            if (_view.HasPassFamily(RenderScenes::RenderViewPassFamily::Models))
            {
                _visibilityPass->Upload(_work, _assets->GetModelGeometryStorage(), *scene);
                _visibilityResolvePass->Upload(_work, _assets->GetModelGeometryStorage(), *scene);
                _rasterDescriptorsReady = _materialResolvePass->Upload(_view, _work, *_materialResources, _assets->GetModelGeometryStorage(), *scene);
            }
            else
            {
                _rasterDescriptorsReady = _forwardPass->Upload(_work, _assets->GetModelGeometryStorage(), *scene);
            }
            _transparentWork.EnsureCapacity(_state.GetQueueCapacity());
            _transparentDescriptorsReady = _transparentPass.Upload(_state, _transparentWork,
                _assets->GetModelGeometryStorage(), *scene);
            if (_transparentSelectionDepth != Renderer::DepthImageID::Invalid())
            {
                _transparentSelectionDescriptorsReady = _transparentSelectionPass.Upload(
                    _transparentWork, _assets->GetModelGeometryStorage(), *scene);
            }
        }
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
        _transparentWork.ReadbackStats(frameIndex);
        _materialResources->ReadbackStats(frameIndex);
        ReportErrors();

        if (!_viewWorkDescriptorsReady || !_rasterDescriptorsReady)
        {
            _view.RequestTemporalReset();
            return;
        }

        const u32 temporalReset = _view.GetTemporalResetGeneration();
        _viewWorkPass.AddHistoryClearPass(renderGraph, _view, _state, _work);
        _viewWorkPass.AddPhase1Pass(renderGraph, resources, _view, _state, _work,
                                    _assets->GetModelGeometryStorage(), _assets->GetMaterialStorage(), *scene,
                                    frameIndex, temporalReset != _handledTemporalReset, _forcedLOD);
        _handledTemporalReset = temporalReset;
        _visibilityPass->AddPass(renderGraph, resources, _view, _work, _assets->GetModelGeometryStorage(),
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
            _visibilityPass->AddPass(renderGraph, resources, _view, _work, _assets->GetModelGeometryStorage(),
                                    _assets->GetMaterialStorage(), scene, frameIndex, 2);
        }
        else
        {
            _readyThisFrame = false;
            const bool visibilityModels = _view.HasPassFamily(RenderScenes::RenderViewPassFamily::Models);
            const bool forwardModels = _view.HasPassFamily(RenderScenes::RenderViewPassFamily::ForwardModels);
            if (!_view.ShouldRender() || (!visibilityModels && !forwardModels))
                return;
            RenderScenes::RenderScene* scene = _view.GetScene();
            if (!scene)
                return;
            _work.ReadbackStats(frameIndex);
            _transparentWork.ReadbackStats(frameIndex);
            ReportErrors();
            if (visibilityModels)
                _materialResources->ReadbackStats(frameIndex);
            if (!_viewWorkDescriptorsReady || !_rasterDescriptorsReady)
            {
                _view.RequestTemporalReset();
                return;
            }
            const u32 temporalReset = _view.GetTemporalResetGeneration();
            _viewWorkPass.AddHistoryClearPass(renderGraph, _view, _state, _work);
            _viewWorkPass.AddPass(renderGraph, resources, _view, _state, _work, _assets->GetModelGeometryStorage(),
                                  _assets->GetMaterialStorage(), *scene, frameIndex,
                                  temporalReset != _handledTemporalReset, _forcedLOD);
            _work.MarkSubmitted(frameIndex);
            _handledTemporalReset = temporalReset;
            if (visibilityModels)
            {
                _visibilityPass->AddPass(renderGraph, resources, _view, _work, _assets->GetModelGeometryStorage(),
                                        _assets->GetMaterialStorage(), *scene, frameIndex);
            }
            else
            {
                _forwardPass->AddPass(renderGraph, resources, _view, _work, _assets->GetModelGeometryStorage(),
                                     _assets->GetMaterialStorage(), *scene, frameIndex);
            }
        }
        if (_view.HasPassFamily(RenderScenes::RenderViewPassFamily::ForwardModels))
        {
            _readyThisFrame = true;
            return;
        }
        RenderScenes::RenderScene& scene = *_view.GetScene();
        _materialResolvePass->AddClassificationPass(renderGraph, resources, _view, _work, *_materialResources,
                                                   _assets->GetModelGeometryStorage(), _assets->GetMaterialStorage(),
                                                   scene, frameIndex);
        _readyThisFrame = true;
    }

    void ModelRenderView::AddPreEffectsPass(Renderer::RenderGraph* renderGraph, RenderResources& resources,
                                             u8 frameIndex)
    {
        if (!_readyThisFrame || !_view.HasPassFamily(RenderScenes::RenderViewPassFamily::Models))
            return;
        RenderScenes::RenderScene& scene = *_view.GetScene();
        _visibilityResolvePass->AddPreEffectsPass(renderGraph, resources, _view, _work,
                                                 _assets->GetModelGeometryStorage(), scene, frameIndex);
    }

    void ModelRenderView::AddMaterialResolvePass(Renderer::RenderGraph* renderGraph, RenderResources& resources,
                                                  u8 frameIndex)
    {
        if (!_readyThisFrame || !_view.HasPassFamily(RenderScenes::RenderViewPassFamily::Models))
            return;
        RenderScenes::RenderScene& scene = *_view.GetScene();
        _materialResolvePass->AddResolvePass(renderGraph, resources, _view, _work, *_materialResources,
                                            _assets->GetModelGeometryStorage(), _assets->GetMaterialStorage(), scene,
                                            frameIndex);
        _visibilityResolvePass->AddOpaqueHighlightPass(renderGraph, _view, _work, scene, frameIndex);
    }

    void ModelRenderView::AddRetainedOutputPass(Renderer::RenderGraph* renderGraph)
    {
        if (!_readyThisFrame)
            return;
        if (_view.HasPassFamily(RenderScenes::RenderViewPassFamily::ForwardModels))
        {
            _view.MarkRendered();
            return;
        }
        _materialResolvePass->AddRetainedOutputPass(renderGraph, _view);
        _view.MarkRendered();
    }

    void ModelRenderView::AddTransparentCullPass(Renderer::RenderGraph* renderGraph, RenderResources& resources,
                                                  u8 frameIndex)
    {
        if (!_readyThisFrame || !_transparentDescriptorsReady)
            return;
        if (_view.GetDepthPyramidTarget() != resources.depthPyramid)
            DepthPyramidUtils::AddBuildPass(renderGraph, "Opaque Depth Pyramid: " + _view.GetDebugName(),
                                            _view.GetDepthTarget(), _view.GetDepthPyramidTarget(), frameIndex);
        RenderScenes::RenderScene& scene = *_view.GetScene();
        _transparentPass.AddCullPass(renderGraph, resources, _view, _state, _transparentWork,
                                     _assets->GetModelGeometryStorage(), _assets->GetMaterialStorage(), scene,
                                     frameIndex);
    }

    void ModelRenderView::AddTransparentRasterPass(Renderer::RenderGraph* renderGraph, RenderResources& resources,
                                                    u8 frameIndex)
    {
        if (!_readyThisFrame || !_transparentDescriptorsReady)
            return;
        RenderScenes::RenderScene& scene = *_view.GetScene();
        _transparentPass.AddRasterPass(renderGraph, resources, _view, _transparentWork,
                                       _assets->GetModelGeometryStorage(), _assets->GetMaterialStorage(), scene,
                                       frameIndex);
        if (_transparentSelectionDepth != Renderer::DepthImageID::Invalid() &&
            _transparentSelectionDescriptorsReady)
        {
            _transparentSelectionPass.AddDepthPass(renderGraph, resources, _view, _transparentWork,
                _assets->GetModelGeometryStorage(), _assets->GetMaterialStorage(), scene,
                _transparentSelectionDepth, frameIndex);
        }
    }

    void ModelRenderView::AddTransparentSelectionOutlinePass(Renderer::RenderGraph* renderGraph,
                                                              RenderResources& resources, u8 frameIndex)
    {
        if (!_readyThisFrame || _transparentSelectionDepth == Renderer::DepthImageID::Invalid() ||
            !_transparentSelectionDescriptorsReady)
            return;
        _transparentSelectionPass.AddOutlinePass(renderGraph, _view, _view.GetTransparencyRevealageTarget(),
                                                 _transparentSelectionDepth, frameIndex);
    }

    void ModelRenderView::AddDiagnosticResolvePass(Renderer::RenderGraph* renderGraph, RenderResources& resources,
                                                    u8 frameIndex)
    {
        if (!_readyThisFrame || !_view.HasPassFamily(RenderScenes::RenderViewPassFamily::Models))
            return;
        RenderScenes::RenderScene& scene = *_view.GetScene();
        _visibilityResolvePass->AddDiagnosticPass(renderGraph, resources, _view, _work,
                                                 _assets->GetModelGeometryStorage(), _assets->GetMaterialStorage(),
                                                 scene, frameIndex);
    }
} // namespace ModelRendering
