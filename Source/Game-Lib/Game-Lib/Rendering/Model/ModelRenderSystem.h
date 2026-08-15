#pragma once

#include "Game-Lib/Rendering/Model/View/ModelRenderView.h"
#include "Game-Lib/Rendering/Scene/RenderView.h"

#include <robinhood/robinhood.h>

#include <memory>

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
        u32 loadedLOD0Meshlets = 0;
        u32 loadedLOD0Triangles = 0;
        u32 loadedLOD0TransparentMeshlets = 0;
        u32 loadedLOD0TransparentTriangles = 0;
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

        void Update();
        void Upload();
        void AdvanceFrame();
        RenderScenes::RenderView* CreateView(const RenderScenes::RenderViewDesc& desc);
        bool DestroyView(u64 viewID);
        RenderScenes::RenderView* GetView(u64 viewID);
        RenderScenes::RenderView* GetMainView() { return _mainView ? &_mainView->GetView() : nullptr; }
        void PreparePreEffectsViews(RenderResources& resources);
        void AddVisibilityPhase1Passes(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);
        void AddVisibilityPhase2Passes(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);
        void AddPreEffectsPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);
        void AddMaterialResolvePass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);
        void AddTransparentCullPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);
        void AddTransparentRasterPass(Renderer::RenderGraph* renderGraph, RenderResources& resources,
                                      RenderScenes::RenderViewPassFamily passFamily, u8 frameIndex);
        void AddTransparencyCompositePasses(Renderer::RenderGraph* renderGraph, RenderResources& resources,
                                            MaterialRenderer& materialRenderer,
                                            RenderScenes::RenderViewPassFamily passFamily, u8 frameIndex);
        void AddTransparentSelectionOutlinePass(Renderer::RenderGraph* renderGraph, RenderResources& resources,
                                                u8 frameIndex);
        void AddRetainedOutputPasses(Renderer::RenderGraph* renderGraph);
        void AddDiagnosticResolvePass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);
        void RegisterPixelQueryResources(Renderer::RenderGraphBuilder& builder) const;
        void BindPixelQueryResources(Renderer::DescriptorSet& descriptorSet);
        void RequestMainViewCameraCut();

        // TODO: Remove this development-only selection hook after GPU work expansion
        // replaces diagnostic work.
        RenderScenes::ModelInstanceHandle SetDiagnosticModel(RenderAssets::ModelHandle model,
                                                             const vec3& worldBoundsCenter, f32 worldBoundsRadius,
                                                             bool geometryGroupsEnabled = true, bool teleported = false);
        const ModelView::WorkStats& GetDiagnosticStats() const
        {
            return _mainView->GetWork().GetStats();
        }
        ModelPerformanceStats GetPerformanceStats() const;

      private:
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
        ModelRenderView* _mainView = nullptr;
        RenderScenes::ModelInstanceHandle _diagnosticInstance = RenderScenes::InvalidModelInstanceHandle();
        i32 _lastForcedLOD = -1;
        bool _validateTransfers = false;
        PixelQueryBindings _pixelQueryBindings;
    };
} // namespace ModelRendering
