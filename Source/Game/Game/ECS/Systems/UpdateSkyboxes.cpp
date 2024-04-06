#include "UpdateSkyboxes.h"

#include "Game/ECS/Components/Camera.h"
#include "Game/ECS/Components/Tags.h"
#include "Game/ECS/Singletons/ActiveCamera.h"
#include "Game/ECS/Singletons/Skybox.h"
#include "Game/ECS/Util/Transforms.h"
#include "Game/Rendering/GameRenderer.h"
#include "Game/Rendering/Model/ModelLoader.h"
#include "Game/Util/ServiceLocator.h"

#include <Base/CVarSystem/CVarSystem.h>

#include <Renderer/RenderSettings.h>

#include <entt/entt.hpp>

AutoCVar_Int CVAR_CameraLockSkybox(CVarCategory::Client | CVarCategory::Rendering, "lockSkybox", "Lock the skybox to not follow the camera", 0, CVarFlags::EditCheckbox);

namespace ECS::Systems
{
    void UpdateSkyboxes::Init(entt::registry& registry)
    {
        entt::registry::context& ctx = registry.ctx();
        Singletons::Skybox& skybox = ctx.emplace<ECS::Singletons::Skybox>();

        GameRenderer* gameRenderer = ServiceLocator::GetGameRenderer();
        ModelLoader* modelLoader = gameRenderer->GetModelLoader();

        skybox.entity = modelLoader->CreateModelEntity("Skybox");
        registry.emplace<ECS::Components::SkyboxModelTag>(skybox.entity);
    }

    void UpdateSkyboxes::Update(entt::registry& registry, f32 deltaTime)
    {
        if (CVAR_CameraLockSkybox.Get())
            return;

        GameRenderer* gameRenderer = ServiceLocator::GetGameRenderer();
        RenderResources& renderResources = gameRenderer->GetRenderResources();

        auto& transformSystem = ECS::TransformSystem::Get(registry);

        entt::registry::context& ctx = registry.ctx();
        Singletons::ActiveCamera& activeCamera = ctx.get<ECS::Singletons::ActiveCamera>();
        Components::Transform& cameraTransform = registry.get<Components::Transform>(activeCamera.entity);
        vec3 cameraPosition = cameraTransform.GetWorldPosition();

        auto view = registry.view<Components::Transform, Components::SkyboxModelTag>();

        view.each([&](entt::entity e, Components::Transform& transform)
        {
            transformSystem.SetWorldPosition(e, cameraPosition);
        });
    }
}
