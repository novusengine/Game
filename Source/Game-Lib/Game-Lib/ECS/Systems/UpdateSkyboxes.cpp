#include "UpdateSkyboxes.h"

#include "Game-Lib/ECS/Components/Camera.h"
#include "Game-Lib/ECS/Components/Tags.h"
#include "Game-Lib/ECS/Singletons/ActiveCamera.h"
#include "Game-Lib/ECS/Singletons/Skybox.h"
#include "Game-Lib/ECS/Util/Transforms.h"
#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Rendering/Model/ModelLoader.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Base/CVarSystem/CVarSystem.h>

#include <Renderer/RenderSettings.h>

#include <entt/entt.hpp>

AutoCVar_Int CVAR_CameraLockSkybox(CVarCategory::Client | CVarCategory::Rendering, "lockSkybox", "Lock the skybox to not follow the camera", 0, CVarFlags::EditCheckbox);

namespace ECS::Systems
{
    void UpdateSkyboxes::Init(entt::registry& registry)
    {
        entt::registry::context& ctx = registry.ctx();
        auto& skybox = ctx.emplace<ECS::Singletons::Skybox>();

        GameRenderer* gameRenderer = ServiceLocator::GetGameRenderer();
        ModelLoader* modelLoader = gameRenderer->GetModelLoader();

        skybox.entity = modelLoader->CreateModelEntity("Skybox");
        registry.emplace<ECS::Components::SkyboxModelTag>(skybox.entity);
    }

    void UpdateSkyboxes::Update(entt::registry& registry, f32 deltaTime)
    {
        ZoneScopedN("ECS::UpdateSkyboxes");

        if (CVAR_CameraLockSkybox.Get())
            return;

        GameRenderer* gameRenderer = ServiceLocator::GetGameRenderer();
        RenderResources& renderResources = gameRenderer->GetRenderResources();

        auto& transformSystem = ECS::TransformSystem::Get(registry);

        entt::registry::context& ctx = registry.ctx();
        auto& activeCamera = ctx.get<ECS::Singletons::ActiveCamera>();
        auto& cameraTransform = registry.get<Components::Transform>(activeCamera.entity);
        vec3 cameraPosition = cameraTransform.GetWorldPosition();

        auto view = registry.view<Components::Transform, Components::SkyboxModelTag>();

        view.each([&](entt::entity e, Components::Transform& transform)
        {
            transformSystem.SetWorldPosition(e, cameraPosition);
        });
    }
}
