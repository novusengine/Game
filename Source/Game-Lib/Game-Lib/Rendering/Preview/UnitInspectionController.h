#pragma once

#include "Game-Lib/Rendering/Preview/UnitRenderDescription.h"

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
namespace RenderScenes { class ScenePreview; }
namespace Renderer { class Renderer; }

namespace PreviewRendering
{
    // Produces registry-independent CPU-side unit descriptions and submits them to a generic Scene preview.
    // Customization and equipment remain in the EnTT-facing adapter while the preview consumes resolved content.
    class UnitInspectionController
    {
      public:
        UnitInspectionController(Renderer::Renderer* renderer, GameRenderer* gameRenderer,
                                 RenderAssets::RenderAssetResources* assets, ModelScene::ModelSceneBridge* worldBridge,
                                 RenderResources& resources, bool validateTransfers = false);
        ~UnitInspectionController();

        bool SetTarget(Renderer::TextureID target);
        void SetUnit(entt::entity unit);
        void Orbit(f32 deltaYaw, f32 deltaPitch);
        void Update(entt::registry& registry);

        // Temporary development helper. Remove after varied customization and equipment are covered by gameplay tests.
        bool BeginDevelopmentGallery(entt::registry& registry, entt::entity unit);

      private:
        bool BuildDescription(entt::registry& registry, entt::entity unit, UnitRenderDescription& description,
                              f32 horizontalOffset = 0.0f, Renderer::TextureID frozenSkin = Renderer::TextureID::Invalid()) const;
        void ApplyDevelopmentGalleryAppearance(entt::registry& registry, u32 appearanceIndex);
        bool CaptureDevelopmentGalleryAppearance(entt::registry& registry, u32 appearanceIndex);
        void RestoreDevelopmentGallerySource(entt::registry& registry);
        void ConnectDirtySignals(entt::registry& registry);
        void OnAppearanceChanged(entt::registry&, entt::entity entity);

        struct DevelopmentGallery
        {
            std::vector<u32> originalEquipment;
            std::array<std::vector<u32>, 3> appearances;
            UnitRenderDescription description;
            u32 appearanceIndex = 0;
            u32 waitFrames = 0;
            bool active = false;
            bool retained = false;
        };

        Renderer::Renderer* _renderer = nullptr;
        RenderAssets::RenderAssetResources* _assets = nullptr;
        ModelScene::ModelSceneBridge* _worldBridge = nullptr;
        std::unique_ptr<RenderScenes::ScenePreview> _preview;
        std::vector<Renderer::TextureID> _developmentGalleryTextures;
        DevelopmentGallery _developmentGallery;
        entt::entity _unit = entt::null;
        entt::registry* _connectedRegistry = nullptr;
        bool _descriptionDirty = true;
        bool _reportedMissingSource = false;
    };
} // namespace PreviewRendering
