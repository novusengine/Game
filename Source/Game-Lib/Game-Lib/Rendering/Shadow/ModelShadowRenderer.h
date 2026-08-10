#pragma once

#include "ModelShadowPass.h"
#include "ModelShadowWorkResources.h"

struct RenderResources;
class GameRenderer;

namespace RenderAssets { class RenderAssetResources; }
namespace RenderScenes { class RenderScene; }
namespace Renderer { class RenderGraph; class Renderer; }

namespace ShadowRendering
{
    // Coordinates shadow-owned model culling resources and raster passes for one Scene.
    // It turns Scene instances into SVSM page writes while specialized pass objects retain GPU policy details.
    class ModelShadowRenderer
    {
      public:
        ModelShadowRenderer(Renderer::Renderer* renderer, GameRenderer* gameRenderer,
                            RenderAssets::RenderAssetResources* assets, RenderScenes::RenderScene* scene);

        void Update(u32 numClipmaps);
        void Upload(Renderer::BufferID svsmData, Renderer::BufferID staticPageTable,
                    Renderer::BufferID dynamicPageTable);
        void AddPasses(Renderer::RenderGraph* renderGraph, RenderResources& resources,
                       Renderer::BufferID svsmData, Renderer::BufferID staticPageTable,
                       Renderer::BufferID dynamicPageTable, Renderer::ImageID staticPagePool,
                       Renderer::ImageID dynamicPagePool, u32 numClipmaps, u32 virtualSize,
                       bool dynamicSplit, u8 frameIndex);

        const ModelShadowStats& GetStats() const { return _work.GetStats(); }

      private:
        Renderer::Renderer* _renderer = nullptr;
        RenderAssets::RenderAssetResources* _assets = nullptr;
        RenderScenes::RenderScene* _scene = nullptr;
        ModelShadowWorkResources _work;
        ModelShadowPass _pass;
        u64 _preparedMembershipRevision = 0;
        u32 _numClipmaps = 0;
        bool _descriptorsReady = false;
    };
} // namespace ShadowRendering
