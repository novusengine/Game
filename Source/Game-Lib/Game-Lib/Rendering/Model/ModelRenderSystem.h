#pragma once

#include "Game-Lib/Rendering/Model/View/ModelRenderView.h"
#include "Game-Lib/Rendering/Scene/RenderView.h"

#include <robinhood/robinhood.h>

#include <memory>
#include <vector>

struct RenderResources;
class GameRenderer;
class MaterialRenderer;

namespace RenderAssets
{
    class RenderAssetResources;
    struct ModelHandle;
} // namespace RenderAssets

namespace RenderScenes
{
    class RenderScene;
}

namespace Animation { class ModelDeformationSystem; }

namespace Renderer
{
    class DescriptorSet;
    class RenderGraph;
    class RenderGraphBuilder;
    class Renderer;
} // namespace Renderer

namespace ModelRendering
{
    struct ModelPerformanceStats
    {
        ModelView::WorkStats work;
        ModelView::TransparentWorkStats transparentWork;
        u32 historyBytes = 0;
        u32 liveHistoryBytes = 0;
        u32 freeHistoryBytes = 0;
        u32 retiredHistoryBytes = 0;
        u32 historyFreeRanges = 0;
    };

    // Coordinates CPU-side model View work and its composed GPU-side passes for the
    // meshlet renderer. It gives GameRenderer one narrow integration point while
    // specialized components retain their own state.
    class ModelRenderSystem
    {
      public:
        ModelRenderSystem(Renderer::Renderer* renderer, GameRenderer* gameRenderer,
                          RenderAssets::RenderAssetResources* assets, RenderScenes::RenderScene* scene,
                          RenderResources& resources, bool validateTransfers = false);
        ~ModelRenderSystem();

        void Update(f32 deltaTime);
        void Upload();
        void CompleteFrame(u8 frameIndex);
        void AdvanceFrame(u8 frameIndex);
        RenderScenes::RenderView* CreateView(const RenderScenes::RenderViewDesc& desc);
        bool DestroyView(u64 viewID);
        RenderScenes::RenderView* GetView(u64 viewID);
        bool HasTransparentWork(u64 viewID) const;
        RenderScenes::RenderView* GetMainView() { return _mainView ? &_mainView->GetView() : nullptr; }
        void PreparePreEffectsViews(RenderResources& resources);
        void AddVisibilityPhase1Passes(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);
        void AddDeformationPass(Renderer::RenderGraph* renderGraph);
        void AddVisibilityPhase2Passes(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);
        void AddPreEffectsPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);
        void AddMaterialResolvePass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);
        void AddCommonMaterialResolvePass(Renderer::RenderGraph* renderGraph, RenderResources& resources,
                                          MaterialRenderer& materialRenderer, u8 frameIndex);
        void AddTransparentCullPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);
        void AddTransparentRasterPass(Renderer::RenderGraph* renderGraph, RenderResources& resources,
                                      RenderScenes::RenderViewPassFamily passFamily, u8 frameIndex);
        void AddTransparencyCompositePasses(Renderer::RenderGraph* renderGraph, RenderResources& resources,
                                            MaterialRenderer& materialRenderer,
                                            RenderScenes::RenderViewPassFamily passFamily, u8 frameIndex);
        void AddTransparentSelectionOutlinePass(Renderer::RenderGraph* renderGraph, RenderResources& resources,
                                                u8 frameIndex);
        void AddRetainedOutputPasses(Renderer::RenderGraph* renderGraph);
        void RegisterPixelQueryResources(Renderer::RenderGraphBuilder& builder) const;
        void BindPixelQueryResources(Renderer::DescriptorSet& descriptorSet);
        void RequestMainViewCameraCut();

        ModelView::WorkStats GetMainViewStats() const;
        ModelPerformanceStats GetPerformanceStats() const;
        Animation::ModelDeformationSystem* GetDeformationSystem() { return _deformationSystem.get(); }
        const Animation::ModelDeformationSystem* GetDeformationSystem() const { return _deformationSystem.get(); }

      private:
        u64 _temporalFrame = 0;
        void RebuildSceneList();
        struct PixelQueryBindings
        {
            Renderer::BufferID visibilityRecords0 = Renderer::BufferID::Invalid();
            Renderer::BufferID visibilityRecords1 = Renderer::BufferID::Invalid();
            Renderer::BufferID modelInstances = Renderer::BufferID::Invalid();
        };

        Renderer::Renderer* _renderer = nullptr;
        GameRenderer* _gameRenderer = nullptr;
        RenderAssets::RenderAssetResources* _assets = nullptr;
        RenderScenes::RenderScene* _mainScene = nullptr;
        robin_hood::unordered_flat_map<u64, std::unique_ptr<ModelRenderView>> _views;
        std::vector<RenderScenes::RenderScene*> _scenes;
        std::vector<RenderScenes::RenderView*> _deformationViews;
        RenderResources* _resources = nullptr;
        ModelRenderView* _mainView = nullptr;
        i32 _lastForcedLOD = -1;
        bool _validateTransfers = false;
        PixelQueryBindings _pixelQueryBindings;
        u64 _submittedFrameValues[ModelView::MODEL_VIEW_FRAME_COUNT] = {};
        u64 _nextSubmissionValue = 1;
        std::unique_ptr<Animation::ModelDeformationSystem> _deformationSystem;
    };
} // namespace ModelRendering
