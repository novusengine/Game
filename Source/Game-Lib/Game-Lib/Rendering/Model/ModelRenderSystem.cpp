#include "ModelRenderSystem.h"

#include "Game-Lib/Rendering/Asset/RenderAssetResources.h"
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
        desc.normalTarget = Renderer::ImageID::Invalid();
        desc.motionTarget = Renderer::ImageID::Invalid();
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
            (visibilityModels && forwardModels) ||
            (visibilityModels && desc.visibilityTarget == Renderer::ImageID::Invalid()) ||
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
        RebuildSceneList();
        return result;
    }

    bool ModelRenderSystem::DestroyView(u64 viewID)
    {
        if (!_views.contains(viewID) || (_mainView && _mainView->GetView().GetID() == viewID))
            return false;
        _views.erase(viewID);
        RebuildSceneList();
        return true;
    }

    RenderScenes::RenderView* ModelRenderSystem::GetView(u64 viewID)
    {
        const auto it = _views.find(viewID);
        return it != _views.end() ? &it->second->GetView() : nullptr;
    }

    bool ModelRenderSystem::HasTransparentWork(u64 viewID) const
    {
        const auto it = _views.find(viewID);
        return it != _views.end() && it->second->HasTransparentWork();
    }

    void ModelRenderSystem::Update()
    {
        const uvec2 renderSize = static_cast<uvec2>(_renderer->GetRenderSize());
        for (auto& [viewID, view] : _views)
        {
            if (view->GetView().GetDimensionType() == Renderer::ImageDimensionType::DIMENSION_SCALE_RENDERSIZE)
                view->GetView().SetDimensions(renderSize);
        }

        for (RenderScenes::RenderScene* scene : _scenes)
        {
            const RenderScenes::SceneClearRequests clears = scene->GetPendingClearRequests();
            if (!clears.IsEmpty())
            {
                for (auto& [viewID, view] : _views)
                {
                    if (view->GetView().GetScene() != scene)
                        continue;
                    if (view->GetView().ShouldRender())
                    {
                        view->GetState().QueueTemporalClears(clears.meshletHistoryRanges);
                    }
                    else
                    {
                        view->GetState().AcknowledgeTemporalClears();
                        view->GetView().RequestTemporalReset();
                    }
                }
                scene->AcknowledgeClearsAndPublish();
            }
        }

        const i32 forcedLOD = std::max(CVAR_ModelForceLOD.Get(), -1);
        _lastForcedLOD = forcedLOD;
        for (auto& [viewID, view] : _views)
            view->Update(forcedLOD);
        for (RenderScenes::RenderScene* scene : _scenes)
        {
            scene->AcknowledgeModelMembershipChanges();
            scene->AcknowledgeTransparentRoutingChanges();
        }
    }

    void ModelRenderSystem::Upload()
    {
        ZoneScoped;
        for (RenderScenes::RenderScene* scene : _scenes)
            scene->SyncToGPU(_renderer);
        for (auto& [viewID, view] : _views)
            view->Upload();
    }

    void ModelRenderSystem::CompleteFrame(u8 frameIndex)
    {
        for (RenderScenes::RenderScene* scene : _scenes)
            scene->ReleaseRetiredHistory(_submittedFrameValues[frameIndex]);
    }

    void ModelRenderSystem::AdvanceFrame(u8 frameIndex)
    {
        const u64 submissionValue = _nextSubmissionValue++;
        _submittedFrameValues[frameIndex] = submissionValue;
        for (RenderScenes::RenderScene* scene : _scenes)
        {
            scene->SetHistoryRetireValue(submissionValue);
            scene->AdvanceFrame();
        }
    }

    void ModelRenderSystem::RebuildSceneList()
    {
        _scenes.clear();
        _scenes.reserve(_views.size());
        for (const auto& [viewID, view] : _views)
        {
            RenderScenes::RenderScene* scene = view->GetView().GetScene();
            if (std::find(_scenes.begin(), _scenes.end(), scene) == _scenes.end())
                _scenes.push_back(scene);
        }
    }

    void ModelRenderSystem::AddVisibilityPhase1Passes(Renderer::RenderGraph* renderGraph, RenderResources& resources,
                                                      u8 frameIndex)
    {
        if (CVAR_ModelTemporalOcclusion.Get() != 0)
            _mainView->AddVisibilityPhase1Passes(renderGraph, resources, frameIndex);
    }

    void ModelRenderSystem::AddVisibilityPhase2Passes(Renderer::RenderGraph* renderGraph, RenderResources& resources,
                                                      u8 frameIndex)
    {
        for (auto& [viewID, view] : _views)
            view->AddVisibilityPhase2Passes(renderGraph, resources, frameIndex,
                                            view.get() == _mainView && CVAR_ModelTemporalOcclusion.Get() != 0);
    }

    void ModelRenderSystem::AddPreEffectsPass(Renderer::RenderGraph* renderGraph, RenderResources& resources,
                                              u8 frameIndex)
    {
        for (auto& [viewID, view] : _views)
            view->AddPreEffectsPass(renderGraph, resources, frameIndex);
    }

    void ModelRenderSystem::PreparePreEffectsViews(RenderResources& resources)
    {
        for (auto& [viewID, view] : _views)
        {
            RenderScenes::RenderView& renderView = view->GetView();
            renderView.PrepareTemporalCamera(resources.cameras[renderView.GetCameraIndex()].worldToClip);
        }
    }

    void ModelRenderSystem::AddMaterialResolvePass(Renderer::RenderGraph* renderGraph, RenderResources& resources,
                                                   u8 frameIndex)
    {
        for (auto& [viewID, view] : _views)
            view->AddMaterialResolvePass(renderGraph, resources, frameIndex);
    }

    void ModelRenderSystem::AddCommonMaterialResolvePass(Renderer::RenderGraph* renderGraph,
                                                         RenderResources& resources,
                                                         MaterialRenderer& materialRenderer, u8 frameIndex)
    {
        _mainView->AddCommonMaterialResolvePass(renderGraph, resources, materialRenderer, frameIndex);
    }

    void ModelRenderSystem::AddTransparentCullPass(Renderer::RenderGraph* renderGraph, RenderResources& resources,
                                                    u8 frameIndex)
    {
        for (auto& [viewID, view] : _views)
            view->AddTransparentCullPass(renderGraph, resources, frameIndex);
    }

    void ModelRenderSystem::AddTransparentRasterPass(Renderer::RenderGraph* renderGraph, RenderResources& resources,
                                                      RenderScenes::RenderViewPassFamily passFamily, u8 frameIndex)
    {
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
        for (auto& [viewID, view] : _views)
        {
            if (view->IsReadyThisFrame() && view->HasTransparentWork() && view->GetView().HasPassFamily(passFamily))
                materialRenderer.AddTransparencyCompositePass(renderGraph, resources, view->GetView(), frameIndex);
        }
    }

    void ModelRenderSystem::AddTransparentSelectionOutlinePass(Renderer::RenderGraph* renderGraph,
                                                                RenderResources& resources, u8 frameIndex)
    {
        for (auto& [viewID, view] : _views)
            view->AddTransparentSelectionOutlinePass(renderGraph, resources, frameIndex);
    }

    void ModelRenderSystem::AddRetainedOutputPasses(Renderer::RenderGraph* renderGraph)
    {
        for (auto& [viewID, view] : _views)
            view->AddRetainedOutputPass(renderGraph);
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
        auto Bind = [&descriptorSet](StringUtils::StringHash name, Renderer::BufferID buffer,
                                     Renderer::BufferID& current) {
            if (buffer == current)
                return;
            descriptorSet.Bind(name, buffer);
            current = buffer;
        };

        Bind("_queryModelVisibilityRecords0"_h, _mainView->GetWork().GetVisibilityRecords(0),
             _pixelQueryBindings.visibilityRecords0);
        Bind("_queryModelVisibilityRecords1"_h, _mainView->GetWork().GetVisibilityRecords(1),
             _pixelQueryBindings.visibilityRecords1);
        Bind("_queryModelInstances"_h, _mainScene->GetModelInstances().GetRecords().GetBuffer(),
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
        result.work = GetMainViewStats();
        result.transparentWork = _mainView->GetTransparentWork().GetStats();
        result.historyBytes = _mainView->GetWork().GetMeshletHistoryWords() * sizeof(u32) * ModelView::MODEL_VIEW_FRAME_COUNT;
        const ModelScene::MeshletHistoryAllocatorStats history = _mainScene->GetStats().meshletHistory;
        result.liveHistoryBytes = history.liveWords * sizeof(u32) * ModelView::MODEL_VIEW_FRAME_COUNT;
        result.freeHistoryBytes = history.freeWords * sizeof(u32) * ModelView::MODEL_VIEW_FRAME_COUNT;
        result.retiredHistoryBytes = history.retiredWords * sizeof(u32) * ModelView::MODEL_VIEW_FRAME_COUNT;
        result.historyFreeRanges = history.freeRanges;
        return result;
    }

    ModelView::WorkStats ModelRenderSystem::GetMainViewStats() const
    {
        ModelView::WorkStats stats = _mainView->GetWork().GetStats();
        stats.phase1ReplayedMeshlets = stats.survivorCount - std::min(stats.phase2AddedMeshlets, stats.survivorCount);
        stats.phase1ReplayedTriangles = stats.committedTriangles -
            std::min(stats.phase2AddedTriangles, stats.committedTriangles);
        return stats;
    }

} // namespace ModelRendering
