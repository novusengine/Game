#pragma once

#include "Game-Lib/Rendering/Scene/RenderSceneHandles.h"

#include <Renderer/Descriptors/TextureDesc.h>

#include <memory>
#include <span>
#include <string>
#include <vector>

struct RenderResources;
class GameRenderer;

namespace RenderAssets { class RenderAssetResources; }
namespace Renderer { class Renderer; }

namespace RenderScenes
{
    class OffscreenRenderView;
    class OrbitCamera;
    class RenderScene;
    struct SceneRenderDescription;

    // Owns a CPU-side Scene and GPU-backed offscreen View for rendering arbitrary described content into a texture.
    // Retained refresh and orbit controls let inspection and editor consumers share the same rendering path.
    class ScenePreview
    {
      public:
        ScenePreview(Renderer::Renderer* renderer, GameRenderer* gameRenderer, RenderAssets::RenderAssetResources* assets,
                     RenderResources& resources, std::string debugName, u64 sceneID, bool validateTransfers = false);
        ~ScenePreview();

        bool SetTarget(Renderer::TextureID target);
        bool SetContent(const SceneRenderDescription& description);
        void Clear();
        void Orbit(f32 deltaYaw, f32 deltaPitch);
        RenderScene* GetScene() const { return _scene.get(); }
        std::span<const ModelInstanceHandle> GetInstances() const { return _instances; }

      private:
        Renderer::Renderer* _renderer = nullptr;
        RenderAssets::RenderAssetResources* _assets = nullptr;
        RenderResources* _resources = nullptr;
        std::unique_ptr<RenderScene> _scene;
        std::unique_ptr<OffscreenRenderView> _renderView;
        std::unique_ptr<OrbitCamera> _camera;
        std::vector<ModelInstanceHandle> _instances;
        u64 _contentRevision = 0;
        bool _validateTransfers = false;
    };
} // namespace RenderScenes
