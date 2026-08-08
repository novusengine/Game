#include "ModelRenderSystem.h"

#include "Game-Lib/Rendering/Asset/RenderAssetResources.h"
#include "Game-Lib/Rendering/RenderResources.h"
#include "Game-Lib/Rendering/Scene/RenderScene.h"

#include <Base/CVarSystem/CVarSystem.h>
#include <Renderer/DescriptorSet.h>
#include <Renderer/RenderGraph.h>
#include <algorithm>

AutoCVar_Int CVAR_ModelForceLOD(CVarCategory::Client | CVarCategory::Rendering, "modelForceLOD",
                                "Force model LOD (-1 automatic, values clamp to each Mesh)", -1);

namespace ModelRendering
{
    ModelRenderSystem::ModelRenderSystem(Renderer::Renderer* renderer, GameRenderer* gameRenderer,
                                         RenderAssets::RenderAssetResources* assets, RenderScenes::RenderScene* scene,
                                         RenderResources& resources, bool validateTransfers)
        : _renderer(renderer), _assets(assets), _scene(scene),
          _mainView(RenderScenes::RenderViewDesc{.viewID = 1,
                                                 .scene = scene,
                                                 .cameraIndex = 0,
                                                 .colorTarget = resources.sceneColor,
                                                 .depthTarget = resources.depth,
                                                 .lifetime = RenderScenes::RenderViewLifetime::Persistent}),
          _mainViewState(validateTransfers), _mainViewWork(renderer), _mainViewMaterialResources(renderer),
          _viewWorkPass(renderer, gameRenderer), _visibilityPass(renderer, gameRenderer),
          _materialResolvePass(renderer, gameRenderer), _visibilityResolvePass(renderer, gameRenderer)
    {
    }

    void ModelRenderSystem::Update()
    {
        if (!_scene->GetPendingClearRequests().IsEmpty())
        {
            // Meshlet history is not consumed until the occlusion phase. Publishing
            // here makes the fully uploaded instance records visible without
            // introducing a temporary per-placement path.
            _scene->AcknowledgeClearsAndPublish();
        }

        if (_mainViewState.IsWorkDirty() ||
            _mainViewState.GetPreparedSceneRevision() != _scene->GetModelInstances().GetMembershipRevision())
            _mainViewState.PrepareInputs(*_scene, _assets->GetModelGeometryStorage());

        const i32 forcedLOD = std::max(CVAR_ModelForceLOD.Get(), -1);
        if (forcedLOD != _lastForcedLOD)
        {
            _lastForcedLOD = forcedLOD;
            _mainView.RequestTemporalReset();
            _mainViewState.ResetLODHistory();
        }
    }

    void ModelRenderSystem::Upload()
    {
        _mainViewState.SyncToGPU(_renderer);
    }

    void ModelRenderSystem::AddVisibilityPasses(Renderer::RenderGraph* renderGraph, RenderResources& resources,
                                                u8 frameIndex)
    {
        _mainViewWork.ReadbackStats(frameIndex);
        _mainViewMaterialResources.ReadbackStats(frameIndex);
        const u32 queueOverflows = _mainViewWork.GetStats().queueOverflows;
        if (queueOverflows > 0 && !_reportedQueueOverflow)
        {
            NC_LOG_ERROR("MODEL_VIEW queue_overflow dropped={}", queueOverflows);
            _reportedQueueOverflow = true;
        }
        else if (queueOverflows == 0)
        {
            _reportedQueueOverflow = false;
        }
        const ModelView::WorkStats& workStats = _mainViewWork.GetStats();
        if (workStats.visibilityRecordOverflows > 0 && !_reportedVisibilityRecordOverflow)
        {
            u32 committedMeshlets = 0;
            for (u32 count : workStats.committedRasterMeshlets)
                committedMeshlets += count;
            NC_LOG_ERROR("MODEL_VIEW visibility_record_overflow dropped={} "
                         "committed={} capacity={}",
                         workStats.visibilityRecordOverflows, committedMeshlets, workStats.visibilityRecords);
            _reportedVisibilityRecordOverflow = true;
        }
        else if (workStats.visibilityRecordOverflows == 0)
        {
            _reportedVisibilityRecordOverflow = false;
        }
        if (workStats.visibilityRecordPackingFailures > 0 && !_reportedVisibilityRecordPackingFailure)
        {
            NC_LOG_ERROR("MODEL_VIEW visibility_record_packing_failure dropped={}",
                         workStats.visibilityRecordPackingFailures);
            _reportedVisibilityRecordPackingFailure = true;
        }
        else if (workStats.visibilityRecordPackingFailures == 0)
        {
            _reportedVisibilityRecordPackingFailure = false;
        }
        const bool descriptorsReady =
            _viewWorkPass.Upload(_mainViewState, _mainViewWork, _assets->GetModelGeometryStorage(), *_scene);
        _visibilityPass.Upload(_mainViewWork, _assets->GetModelGeometryStorage(), *_scene);
        _visibilityResolvePass.Upload(_mainViewWork, _assets->GetModelGeometryStorage(), *_scene);
        const bool materialDescriptorsReady = _materialResolvePass.Upload(_mainViewWork, _mainViewMaterialResources,
                                                                          _assets->GetModelGeometryStorage(), *_scene);
        if (!descriptorsReady || !materialDescriptorsReady)
            return;
        const u32 temporalReset = _mainView.GetTemporalResetGeneration();
        _viewWorkPass.AddPass(renderGraph, resources, _mainView, _mainViewState, _mainViewWork,
                              _assets->GetModelGeometryStorage(), _assets->GetMaterialStorage(), *_scene, frameIndex,
                              temporalReset != _handledTemporalReset, _lastForcedLOD);
        _mainViewWork.MarkSubmitted(frameIndex);
        _handledTemporalReset = temporalReset;
        _visibilityPass.AddPass(renderGraph, resources, _mainView, _mainViewWork, _assets->GetModelGeometryStorage(),
                                _assets->GetMaterialStorage(), *_scene, frameIndex);
        _materialResolvePass.AddClassificationPass(renderGraph, resources, _mainView, _mainViewWork,
                                                   _mainViewMaterialResources, _assets->GetModelGeometryStorage(),
                                                   _assets->GetMaterialStorage(), *_scene, frameIndex);
    }

    void ModelRenderSystem::AddPreEffectsPass(Renderer::RenderGraph* renderGraph, RenderResources& resources,
                                              u8 frameIndex)
    {
        _visibilityResolvePass.AddPreEffectsPass(renderGraph, resources, _mainView, _mainViewWork,
                                                 _assets->GetModelGeometryStorage(), *_scene, frameIndex);
    }

    void ModelRenderSystem::AddMaterialResolvePass(Renderer::RenderGraph* renderGraph, RenderResources& resources,
                                                   u8 frameIndex)
    {
        _materialResolvePass.AddResolvePass(renderGraph, resources, _mainView, _mainViewWork,
                                            _mainViewMaterialResources, _assets->GetModelGeometryStorage(),
                                            _assets->GetMaterialStorage(), *_scene, frameIndex);
    }

    void ModelRenderSystem::AddDiagnosticResolvePass(Renderer::RenderGraph* renderGraph, RenderResources& resources,
                                                     u8 frameIndex)
    {
        _visibilityResolvePass.AddDiagnosticPass(renderGraph, resources, _mainView, _mainViewWork,
                                                 _assets->GetModelGeometryStorage(), _assets->GetMaterialStorage(),
                                                 *_scene, frameIndex);
    }

    void ModelRenderSystem::RegisterPixelQueryResources(Renderer::RenderGraphBuilder& builder) const
    {
        using Usage = Renderer::BufferPassUsage;
        builder.Read(_mainViewWork.GetVisibilityRecords(0), Usage::COMPUTE);
        builder.Read(_mainViewWork.GetVisibilityRecords(1), Usage::COMPUTE);
        builder.Read(_scene->GetModelInstances().GetRecords().GetBuffer(), Usage::COMPUTE);
    }

    void ModelRenderSystem::BindPixelQueryResources(Renderer::DescriptorSet& descriptorSet)
    {
        auto bind = [&descriptorSet](StringUtils::StringHash name, Renderer::BufferID buffer,
                                     Renderer::BufferID& current) {
            if (buffer == current)
                return;
            descriptorSet.Bind(name, buffer);
            current = buffer;
        };

        bind("_queryModelVisibilityRecords0"_h, _mainViewWork.GetVisibilityRecords(0),
             _pixelQueryBindings.visibilityRecords0);
        bind("_queryModelVisibilityRecords1"_h, _mainViewWork.GetVisibilityRecords(1),
             _pixelQueryBindings.visibilityRecords1);
        bind("_queryModelInstances"_h, _scene->GetModelInstances().GetRecords().GetBuffer(),
             _pixelQueryBindings.modelInstances);
    }

    RenderScenes::ModelInstanceHandle ModelRenderSystem::SetDiagnosticModel(RenderAssets::ModelHandle model,
                                                                            const vec3& worldBoundsCenter,
                                                                            f32 worldBoundsRadius,
                                                                            bool geometryGroupsEnabled)
    {
        const ModelLoading::ModelGeometryStorage& geometry = _assets->GetModelGeometryStorage();
        if (!geometry.HasModel(model) || worldBoundsRadius <= 0.0f)
            return RenderScenes::InvalidModelInstanceHandle();

        if (_scene->IsAlive(_diagnosticInstance) || _scene->IsPending(_diagnosticInstance))
        {
            _scene->DestroyModelInstance(_diagnosticInstance, 0);
            _scene->ReleaseRetiredHistory(0);
        }

        const FileFormat::Model::Bounds& bounds = geometry.GetRecord(model).bounds;
        const f32 scale = worldBoundsRadius / std::max(bounds.sphereRadius, 0.000001f);
        mat4x4 transform(scale);
        transform[3] = vec4(worldBoundsCenter - bounds.center * scale, 1.0f);

        RenderScenes::ModelInstanceDesc desc;
        desc.model = model;
        desc.worldTransform = transform;
        _diagnosticInstance = _scene->CreateModelInstance(desc);
        if (!_scene->IsPending(_diagnosticInstance))
            return RenderScenes::InvalidModelInstanceHandle();
        _scene->SetAllGeometryGroups(_diagnosticInstance, geometryGroupsEnabled);

        // No temporal View buffers exist during diagnostic bring-up, so acknowledging
        // these ranges publishes the instance after its complete record is uploaded
        // and before it can be consumed by this pass.
        _scene->AcknowledgeClearsAndPublish();
        _mainViewState.SetDiagnosticSelection(_diagnosticInstance);
        return _diagnosticInstance;
    }
} // namespace ModelRendering
