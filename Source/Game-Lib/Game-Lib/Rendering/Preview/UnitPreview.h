#pragma once

#include "Game-Lib/Rendering/Scene/RenderSceneHandles.h"

#include <Renderer/Descriptors/TextureDesc.h>

#include <entt/entity/entity.hpp>
#include <entt/fwd.hpp>

#include <array>
#include <memory>
#include <vector>

struct RenderResources;
class GameRenderer;

namespace ModelScene { class ModelSceneBridge; }
namespace RenderAssets { class RenderAssetResources; }
namespace RenderScenes { class OffscreenRenderView; class OrbitCamera; class RenderScene; }
namespace Renderer { class Renderer; }

namespace PreviewRendering
{
    // Coordinates Unit-specific Scene mirroring with a generic offscreen View and orbit camera for UI inspection.
    // It keeps material selections and geometry groups synchronized while sharing loaded GPU assets.
    class UnitPreview
    {
      public:
        UnitPreview(Renderer::Renderer* renderer, GameRenderer* gameRenderer, RenderAssets::RenderAssetResources* assets,
                    ModelScene::ModelSceneBridge* worldBridge, RenderScenes::RenderScene* worldScene,
                    RenderResources& resources, bool validateTransfers = false);
        ~UnitPreview();

        bool SetTarget(Renderer::TextureID target);
        void SetUnit(entt::entity unit);
        void Orbit(f32 deltaYaw, f32 deltaPitch);
        void Update(entt::registry& registry);

        // Temporary Phase 11 bring-up helper. Remove after multi-instance appearance selection is covered by normal gameplay.
        bool BeginDevelopmentGallery(entt::registry& registry, entt::entity unit);

      private:
        struct SourceInstance
        {
            entt::entity entity = entt::null;
            RenderScenes::ModelInstanceHandle handle = RenderScenes::InvalidModelInstanceHandle();
            u64 appearanceHash = 0;

            bool operator==(const SourceInstance&) const = default;
        };

        void CollectSourceInstances(entt::registry& registry, std::vector<SourceInstance>& instances) const;
        void RebuildScene(entt::registry& registry, const std::vector<SourceInstance>& instances);
        void ApplyDevelopmentGalleryAppearance(entt::registry& registry, u32 appearanceIndex);
        bool CaptureDevelopmentGalleryAppearance(entt::registry& registry, u32 appearanceIndex);
        void RestoreDevelopmentGallerySource(entt::registry& registry);
        struct DevelopmentGallery
        {
            std::vector<u32> originalEquipment;
            std::array<std::vector<u32>, 3> appearances;
            u32 appearanceIndex = 0;
            u32 waitFrames = 0;
            bool active = false;
            bool retained = false;
        };

        Renderer::Renderer* _renderer = nullptr;
        RenderAssets::RenderAssetResources* _assets = nullptr;
        ModelScene::ModelSceneBridge* _worldBridge = nullptr;
        RenderScenes::RenderScene* _worldScene = nullptr;
        RenderResources* _resources = nullptr;
        std::unique_ptr<RenderScenes::RenderScene> _scene;
        std::unique_ptr<RenderScenes::OffscreenRenderView> _renderView;
        std::unique_ptr<RenderScenes::OrbitCamera> _camera;
        std::vector<SourceInstance> _sourceInstances;
        std::vector<RenderScenes::ModelInstanceHandle> _previewInstances;
        std::vector<Renderer::TextureID> _developmentGalleryTextures;
        DevelopmentGallery _developmentGallery;
        entt::entity _unit = entt::null;
        bool _validateTransfers = false;
        bool _reportedMissingSource = false;
    };
} // namespace PreviewRendering
