#include "ModelRenderSystem.h"

#include "Game-Lib/Rendering/Asset/RenderAssetResources.h"
#include "Game-Lib/Rendering/Model/ModelRendererMode.h"
#include "Game-Lib/Rendering/Material/MaterialRenderer.h"
#include "Game-Lib/Rendering/RenderResources.h"
#include "Game-Lib/Rendering/Scene/RenderScene.h"

#include <Base/CVarSystem/CVarSystem.h>
#include <Renderer/DescriptorSet.h>
#include <Renderer/RenderGraph.h>
#include <algorithm>

AutoCVar_Int CVAR_ModelForceLOD(CVarCategory::Client | CVarCategory::Rendering, "modelForceLOD",
                                "Force model LOD (-1 automatic, values clamp to each Mesh)", 0);
AutoCVar_Int CVAR_ModelTemporalOcclusion(CVarCategory::Client | CVarCategory::Rendering, "modelTemporalOcclusion",
                                         "Enable two-phase temporal occlusion for the main model View", 1,
                                         CVarFlags::EditCheckbox);
// TODO: Remove this diagnostic-model highlight control after the Phase 14 visual comparison.
AutoCVar_Int CVAR_ModelDiagnosticHighlight(CVarCategory::Client | CVarCategory::Rendering,
                                           "modelDiagnosticHighlight", "Highlight the diagnostic model", 0,
                                           CVarFlags::EditCheckbox);
// TODO: Remove this diagnostic-model opacity control after the Phase 15 fade comparison.
AutoCVar_Float CVAR_ModelDiagnosticOpacity(CVarCategory::Client | CVarCategory::Rendering,
                                           "modelDiagnosticOpacity", "Opacity of the diagnostic model", 1.0);

namespace ModelRendering
{
    ModelRenderSystem::ModelRenderSystem(Renderer::Renderer* renderer, GameRenderer* gameRenderer,
                                         RenderAssets::RenderAssetResources* assets, RenderScenes::RenderScene* scene,
                                         RenderResources& resources, bool validateTransfers)
        : _renderer(renderer), _gameRenderer(gameRenderer), _assets(assets), _mainScene(scene),
          _validateTransfers(validateTransfers)
    {
        RenderScenes::RenderViewDesc desc;
        desc.viewID = 1;
        desc.debugName = "Game";
        desc.scene = scene;
        desc.cameraIndex = 0;
        desc.dimensions = static_cast<uvec2>(renderer->GetRenderSize());
        desc.dimensionType = Renderer::ImageDimensionType::DIMENSION_SCALE_RENDERSIZE;
        desc.visibilityTarget = resources.visibilityBuffer;
        desc.normalTarget = resources.packedNormals;
        desc.motionTarget = resources.motionVectors;
        desc.colorTarget = resources.sceneColor;
        desc.transparencyAccumulationTarget = resources.transparency;
        desc.transparencyRevealageTarget = resources.transparencyWeights;
        desc.depthPyramidTarget = resources.depthPyramid;
        desc.depthTarget = resources.depth;
        desc.lifetime = RenderScenes::RenderViewLifetime::Persistent;
        desc.refresh = RenderScenes::RenderViewRefresh::Continuous;
        desc.worldShadows = true;
        CreateView(desc);
        _mainView = _views.at(desc.viewID).get();
    }

    RenderScenes::RenderView* ModelRenderSystem::CreateView(const RenderScenes::RenderViewDesc& desc)
    {
        const bool visibilityModels = (static_cast<u32>(desc.passFamilies) & static_cast<u32>(RenderScenes::RenderViewPassFamily::Models)) != 0;
        const bool forwardModels = (static_cast<u32>(desc.passFamilies) & static_cast<u32>(RenderScenes::RenderViewPassFamily::ForwardModels)) != 0;
        if (desc.viewID == 0 || desc.debugName.empty() || !desc.scene || desc.dimensions.x == 0 || desc.dimensions.y == 0 ||
            (!visibilityModels && !forwardModels) ||
            (visibilityModels && (desc.visibilityTarget == Renderer::ImageID::Invalid() || desc.normalTarget == Renderer::ImageID::Invalid())) ||
            desc.colorTarget == Renderer::ImageID::Invalid() ||
            desc.transparencyAccumulationTarget == Renderer::ImageID::Invalid() ||
            desc.transparencyRevealageTarget == Renderer::ImageID::Invalid() ||
            desc.depthPyramidTarget == Renderer::ImageID::Invalid() ||
            desc.depthTarget == Renderer::DepthImageID::Invalid() || _views.contains(desc.viewID))
        {
            return nullptr;
        }

        auto view = std::make_unique<ModelRenderView>(_renderer, _gameRenderer, _assets, desc, _validateTransfers);
        RenderScenes::RenderView* result = &view->GetView();
        _views.emplace(desc.viewID, std::move(view));
        return result;
    }

    bool ModelRenderSystem::DestroyView(u64 viewID)
    {
        if (!_views.contains(viewID) || (_mainView && _mainView->GetView().GetID() == viewID))
            return false;
        _views.erase(viewID);
        return true;
    }

    RenderScenes::RenderView* ModelRenderSystem::GetView(u64 viewID)
    {
        const auto it = _views.find(viewID);
        return it != _views.end() ? &it->second->GetView() : nullptr;
    }

    void ModelRenderSystem::Update()
    {
        const uvec2 renderSize = static_cast<uvec2>(_renderer->GetRenderSize());
        for (auto& [viewID, view] : _views)
        {
            if (view->GetView().GetDimensionType() == Renderer::ImageDimensionType::DIMENSION_SCALE_RENDERSIZE)
                view->GetView().SetDimensions(renderSize);
        }

        robin_hood::unordered_flat_set<RenderScenes::RenderScene*> scenes;
        for (auto& [viewID, view] : _views)
            scenes.insert(view->GetView().GetScene());
        for (RenderScenes::RenderScene* scene : scenes)
        {
            if (!scene->GetPendingClearRequests().IsEmpty())
            {
                for (auto& [viewID, view] : _views)
                {
                    if (view->GetView().GetScene() == scene)
                        view->GetView().RequestTemporalReset();
                }
                scene->AcknowledgeClearsAndPublish();
            }
        }

        const i32 forcedLOD = std::max(CVAR_ModelForceLOD.Get(), -1);
        _lastForcedLOD = forcedLOD;
        for (auto& [viewID, view] : _views)
            view->Update(forcedLOD);
    }

    void ModelRenderSystem::Upload()
    {
        robin_hood::unordered_flat_set<RenderScenes::RenderScene*> scenes;
        for (auto& [viewID, view] : _views)
            scenes.insert(view->GetView().GetScene());
        for (RenderScenes::RenderScene* scene : scenes)
            scene->SyncToGPU(_renderer);
        for (auto& [viewID, view] : _views)
            view->Upload();
    }

    void ModelRenderSystem::AdvanceFrame()
    {
        robin_hood::unordered_flat_set<RenderScenes::RenderScene*> scenes;
        for (auto& [viewID, view] : _views)
            scenes.insert(view->GetView().GetScene());
        for (RenderScenes::RenderScene* scene : scenes)
            scene->AdvanceFrame();
    }

    void ModelRenderSystem::AddVisibilityPhase1Passes(Renderer::RenderGraph* renderGraph, RenderResources& resources,
                                                      u8 frameIndex)
    {
        if (!UseMeshletModelRenderer())
            return;
        if (CVAR_ModelTemporalOcclusion.Get() != 0)
            _mainView->AddVisibilityPhase1Passes(renderGraph, resources, frameIndex);
    }

    void ModelRenderSystem::AddVisibilityPhase2Passes(Renderer::RenderGraph* renderGraph, RenderResources& resources,
                                                      u8 frameIndex)
    {
        if (!UseMeshletModelRenderer())
            return;
        for (auto& [viewID, view] : _views)
            view->AddVisibilityPhase2Passes(renderGraph, resources, frameIndex,
                                            view.get() == _mainView && CVAR_ModelTemporalOcclusion.Get() != 0);
    }

    void ModelRenderSystem::AddPreEffectsPass(Renderer::RenderGraph* renderGraph, RenderResources& resources,
                                              u8 frameIndex)
    {
        if (!UseMeshletModelRenderer())
            return;
        for (auto& [viewID, view] : _views)
            view->AddPreEffectsPass(renderGraph, resources, frameIndex);
    }

    void ModelRenderSystem::PreparePreEffectsViews(RenderResources& resources)
    {
        if (!UseMeshletModelRenderer())
            return;
        for (auto& [viewID, view] : _views)
        {
            RenderScenes::RenderView& renderView = view->GetView();
            renderView.PrepareTemporalCamera(resources.cameras[renderView.GetCameraIndex()].worldToClip);
        }
    }

    void ModelRenderSystem::AddMaterialResolvePass(Renderer::RenderGraph* renderGraph, RenderResources& resources,
                                                   u8 frameIndex)
    {
        if (!UseMeshletModelRenderer())
            return;
        for (auto& [viewID, view] : _views)
            view->AddMaterialResolvePass(renderGraph, resources, frameIndex);
    }

    void ModelRenderSystem::AddTransparentCullPass(Renderer::RenderGraph* renderGraph, RenderResources& resources,
                                                    u8 frameIndex)
    {
        if (!UseMeshletModelRenderer())
            return;
        for (auto& [viewID, view] : _views)
            view->AddTransparentCullPass(renderGraph, resources, frameIndex);
    }

    void ModelRenderSystem::AddTransparentRasterPass(Renderer::RenderGraph* renderGraph, RenderResources& resources,
                                                      RenderScenes::RenderViewPassFamily passFamily, u8 frameIndex)
    {
        if (!UseMeshletModelRenderer())
            return;
        for (auto& [viewID, view] : _views)
        {
            if (view->GetView().HasPassFamily(passFamily))
                view->AddTransparentRasterPass(renderGraph, resources, frameIndex);
        }
    }

    void ModelRenderSystem::AddTransparencyCompositePasses(Renderer::RenderGraph* renderGraph,
                                                            RenderResources& resources,
                                                            MaterialRenderer& materialRenderer,
                                                            RenderScenes::RenderViewPassFamily passFamily, u8 frameIndex)
    {
        if (!UseMeshletModelRenderer())
            return;
        for (auto& [viewID, view] : _views)
        {
            if (view->IsReadyThisFrame() && view->GetView().HasPassFamily(passFamily))
                materialRenderer.AddTransparencyCompositePass(renderGraph, resources, view->GetView(), frameIndex);
        }
    }

    void ModelRenderSystem::AddTransparentSelectionOutlinePass(Renderer::RenderGraph* renderGraph,
                                                                RenderResources& resources, u8 frameIndex)
    {
        if (!UseMeshletModelRenderer())
            return;
        for (auto& [viewID, view] : _views)
            view->AddTransparentSelectionOutlinePass(renderGraph, resources, frameIndex);
    }

    void ModelRenderSystem::AddRetainedOutputPasses(Renderer::RenderGraph* renderGraph)
    {
        if (!UseMeshletModelRenderer())
            return;
        for (auto& [viewID, view] : _views)
            view->AddRetainedOutputPass(renderGraph);
    }

    void ModelRenderSystem::AddDiagnosticResolvePass(Renderer::RenderGraph* renderGraph, RenderResources& resources,
                                                     u8 frameIndex)
    {
        if (!UseMeshletModelRenderer())
            return;
        for (auto& [viewID, view] : _views)
            view->AddDiagnosticResolvePass(renderGraph, resources, frameIndex);
    }

    void ModelRenderSystem::RegisterPixelQueryResources(Renderer::RenderGraphBuilder& builder) const
    {
        using Usage = Renderer::BufferPassUsage;
        builder.Read(_mainView->GetWork().GetVisibilityRecords(0), Usage::COMPUTE);
        builder.Read(_mainView->GetWork().GetVisibilityRecords(1), Usage::COMPUTE);
        builder.Read(_mainScene->GetModelInstances().GetRecords().GetBuffer(), Usage::COMPUTE);
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

        bind("_queryModelVisibilityRecords0"_h, _mainView->GetWork().GetVisibilityRecords(0),
             _pixelQueryBindings.visibilityRecords0);
        bind("_queryModelVisibilityRecords1"_h, _mainView->GetWork().GetVisibilityRecords(1),
             _pixelQueryBindings.visibilityRecords1);
        bind("_queryModelInstances"_h, _mainScene->GetModelInstances().GetRecords().GetBuffer(),
             _pixelQueryBindings.modelInstances);
    }

    void ModelRenderSystem::RequestMainViewCameraCut()
    {
        if (_mainView)
            _mainView->GetView().RequestCameraCut();
    }

    ModelPerformanceStats ModelRenderSystem::GetPerformanceStats() const
    {
        ModelPerformanceStats result;
        result.work = _mainView->GetWork().GetStats();
        result.transparentWork = _mainView->GetTransparentWork().GetStats();
        result.loadedLOD0Meshlets = _mainView->GetState().GetLoadedLOD0Meshlets();
        result.loadedLOD0Triangles = _mainView->GetState().GetLoadedLOD0Triangles();
        result.loadedLOD0TransparentMeshlets = _mainView->GetState().GetLoadedLOD0TransparentMeshlets();
        result.loadedLOD0TransparentTriangles = _mainView->GetState().GetLoadedLOD0TransparentTriangles();
        result.historyBytes = _mainView->GetWork().GetMeshletHistoryWords() * sizeof(u32) * ModelView::MODEL_VIEW_FRAME_COUNT;
        const ModelScene::MeshletHistoryAllocatorStats history = _mainScene->GetStats().meshletHistory;
        result.liveHistoryBytes = history.liveWords * sizeof(u32) * ModelView::MODEL_VIEW_FRAME_COUNT;
        result.freeHistoryBytes = history.freeWords * sizeof(u32) * ModelView::MODEL_VIEW_FRAME_COUNT;
        result.retiredHistoryBytes = history.retiredWords * sizeof(u32) * ModelView::MODEL_VIEW_FRAME_COUNT;
        result.historyFreeRanges = history.freeRanges;
        return result;
    }

    RenderScenes::ModelInstanceHandle ModelRenderSystem::SetDiagnosticModel(RenderAssets::ModelHandle model,
                                                                            const vec3& worldBoundsCenter,
                                                                            f32 worldBoundsRadius,
                                                                            bool geometryGroupsEnabled,
                                                                            bool teleported)
    {
        const ModelLoading::ModelGeometryStorage& geometry = _assets->GetModelGeometryStorage();
        if (!geometry.HasModel(model) || worldBoundsRadius <= 0.0f)
            return RenderScenes::InvalidModelInstanceHandle();

        const FileFormat::Model::Bounds& bounds = geometry.GetRecord(model).bounds;
        const f32 scale = worldBoundsRadius / std::max(bounds.sphereRadius, 0.000001f);
        mat4x4 transform(scale);
        transform[3] = vec4(worldBoundsCenter - bounds.center * scale, 1.0f);

        if (_mainScene->IsAlive(_diagnosticInstance) || _mainScene->IsPending(_diagnosticInstance))
        {
            const ModelScene::ModelInstanceResources* resources =
                _mainScene->GetModelInstances().GetResources(_diagnosticInstance);
            if (resources && resources->model == model)
            {
                _mainScene->SetModelTransform(_diagnosticInstance, transform, teleported);
                _mainScene->SetAllGeometryGroups(_diagnosticInstance, geometryGroupsEnabled);
                _mainScene->SetModelHighlight(_diagnosticInstance,
                                              CVAR_ModelDiagnosticHighlight.Get() != 0 ? 1.35f : 1.0f);
                _mainScene->SetModelOpacity(_diagnosticInstance,
                                            static_cast<f32>(CVAR_ModelDiagnosticOpacity.Get()));
                _mainView->GetState().SetDiagnosticSelection(_diagnosticInstance);
                return _diagnosticInstance;
            }
            _mainScene->DestroyModelInstance(_diagnosticInstance, 0);
            _mainScene->ReleaseRetiredHistory(0);
        }

        RenderScenes::ModelInstanceDesc desc;
        desc.model = model;
        desc.worldTransform = transform;
        _diagnosticInstance = _mainScene->CreateModelInstance(desc);
        if (!_mainScene->IsPending(_diagnosticInstance))
            return RenderScenes::InvalidModelInstanceHandle();
        _mainScene->SetAllGeometryGroups(_diagnosticInstance, geometryGroupsEnabled);
        _mainScene->SetModelHighlight(_diagnosticInstance,
                                      CVAR_ModelDiagnosticHighlight.Get() != 0 ? 1.35f : 1.0f);
        _mainScene->SetModelOpacity(_diagnosticInstance, static_cast<f32>(CVAR_ModelDiagnosticOpacity.Get()));

        // No temporal View buffers exist during diagnostic bring-up, so acknowledging
        // these ranges publishes the instance after its complete record is uploaded
        // and before it can be consumed by this pass.
        _mainScene->AcknowledgeClearsAndPublish();
        _mainView->GetState().SetDiagnosticSelection(_diagnosticInstance);
        return _diagnosticInstance;
    }
} // namespace ModelRendering
