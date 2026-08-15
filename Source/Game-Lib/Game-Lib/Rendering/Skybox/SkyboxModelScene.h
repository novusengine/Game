#pragma once

#include <Renderer/Descriptors/ImageDesc.h>

struct RenderResources;
class GameRenderer;

namespace RenderAssets { class RenderAssetResources; }
namespace RenderScenes { class RenderScene; }
namespace Renderer { class RenderGraph; class Renderer; }

namespace SkyboxRendering
{
    // Owns the CPU-side Scene and auxiliary GPU targets used by model-based skyboxes.
    // A separate Scene keeps skybox instances out of world culling while a direct-forward View shades them into the shared skybox target.
    class SkyboxModelScene
    {
      public:
        SkyboxModelScene(Renderer::Renderer* renderer, GameRenderer* gameRenderer,
                         RenderAssets::RenderAssetResources* assets, RenderResources& resources,
                         bool validateTransfers = false);
        ~SkyboxModelScene();

        void AddStartFramePass(Renderer::RenderGraph* renderGraph);
        RenderScenes::RenderScene* GetScene() const { return _scene; }

      private:
        Renderer::Renderer* _renderer = nullptr;
        GameRenderer* _gameRenderer = nullptr;
        RenderScenes::RenderScene* _scene = nullptr;
        u64 _viewID = 0;
        Renderer::ImageID _transparencyAccumulation = Renderer::ImageID::Invalid();
        Renderer::ImageID _transparencyRevealage = Renderer::ImageID::Invalid();
        Renderer::ImageID _depthPyramid = Renderer::ImageID::Invalid();
    };
} // namespace SkyboxRendering
