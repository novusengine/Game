#include "Scheduler.h"

#include "Game-Lib/ECS/Singletons/ActiveCamera.h"
#include "Game-Lib/ECS/Singletons/AreaLightInfo.h"
#include "Game-Lib/ECS/Singletons/CharacterSingleton.h"
#include "Game-Lib/ECS/Singletons/DayNightCycle.h"
#include "Game-Lib/ECS/Singletons/EngineStats.h"
#include "Game-Lib/ECS/Singletons/JoltState.h"
#include "Game-Lib/ECS/Singletons/RenderState.h"
#include "Game-Lib/ECS/Components/Camera.h"

#include "Game-Lib/ECS/Systems/UpdateAreaLights.h"
#include "Game-Lib/ECS/Systems/CalculateCameraMatrices.h"
#include "Game-Lib/ECS/Systems/CalculateShadowCameraMatrices.h"
#include "Game-Lib/ECS/Systems/UpdateDayNightCycle.h"
#include "Game-Lib/ECS/Systems/DrawDebugMesh.h"
#include "Game-Lib/ECS/Systems/FreeflyingCamera.h"
#include "Game-Lib/ECS/Systems/OrbitalCamera.h"
#include "Game-Lib/ECS/Systems/NetworkConnection.h"
#include "Game-Lib/ECS/Systems/UpdateNetworkedEntity.h"
#include "Game-Lib/ECS/Systems/UpdatePhysics.h"
#include "Game-Lib/ECS/Systems/UpdateScripts.h"
#include "Game-Lib/ECS/Systems/UpdateSkyboxes.h"
#include "Game-Lib/ECS/Systems/CalculateTransformMatrices.h"
#include "Game-Lib/ECS/Systems/UpdateAABBs.h"
#include "Game-Lib/ECS/Systems/CharacterController.h"
#include "Game-Lib/ECS/Systems/UI/HandleInput.h"
#include "Game-Lib/ECS/Systems/UI/UpdateBoundingRects.h"

#include <Renderer/RenderSettings.h>

#include <entt/entt.hpp>

#include <tracy/Tracy.hpp>

namespace ECS
{
    Scheduler::Scheduler()
    {

    }

    void Scheduler::Init(EnttRegistries& registries)
    {
        // Game
        entt::registry& gameRegistry = *registries.gameRegistry;

        Systems::NetworkConnection::Init(gameRegistry);
        Systems::UpdateNetworkedEntity::Init(gameRegistry);
        Systems::UpdatePhysics::Init(gameRegistry);
        Systems::DrawDebugMesh::Init(gameRegistry);
        Systems::FreeflyingCamera::Init(gameRegistry);
        Systems::OrbitalCamera::Init(gameRegistry);
        Systems::CharacterController::Init(gameRegistry);
        Systems::UpdateScripts::Init(gameRegistry);
        Systems::UpdateDayNightCycle::Init(gameRegistry);
        Systems::UpdateAreaLights::Init(gameRegistry);
        Systems::UpdateSkyboxes::Init(gameRegistry);

        entt::registry::context& ctx = gameRegistry.ctx();
        ctx.emplace<Singletons::EngineStats>();
        ctx.emplace<Singletons::RenderState>();

        // UI
        entt::registry& uiRegistry = *registries.uiRegistry;

        Systems::UI::HandleInput::Init(uiRegistry);
    }

    void Scheduler::Update(EnttRegistries& registries, f32 deltaTime)
    {
        // Game
        entt::registry& gameRegistry = *registries.gameRegistry;

        // TODO: You know, actually scheduling stuff and multithreading (enkiTS tasks?)
        entt::registry::context& ctx = gameRegistry.ctx();
        auto& joltState = ctx.get<Singletons::JoltState>();

        static constexpr f32 maxDeltaTimeDiff = 1.0f / 60.0f;
        f32 clampedDeltaTime = glm::clamp(deltaTime, 0.0f, maxDeltaTimeDiff);

        joltState.updateTimer += glm::clamp(clampedDeltaTime, 0.0f, Singletons::JoltState::FixedDeltaTime);

        {
            ZoneScopedN("UpdateDayNightCycle");
            Systems::UpdateDayNightCycle::Update(gameRegistry, clampedDeltaTime);
        }
        {
            ZoneScopedN("NetworkConnection");
            Systems::NetworkConnection::Update(gameRegistry, clampedDeltaTime);
        }
        {
            ZoneScopedN("DrawDebugMesh");
            Systems::DrawDebugMesh::Update(gameRegistry, clampedDeltaTime);
        }

        {
            ZoneScopedN("CharacterController");
            Systems::CharacterController::Update(gameRegistry, clampedDeltaTime);
        }
        {
            ZoneScopedN("UpdateNetworkedEntity");
            Systems::UpdateNetworkedEntity::Update(gameRegistry, clampedDeltaTime);
        }

        {
            ZoneScopedN("FreeflyingCamera");
            Systems::FreeflyingCamera::Update(gameRegistry, clampedDeltaTime);
        }
        {
            ZoneScopedN("OrbitalCamera");
            Systems::OrbitalCamera::Update(gameRegistry, clampedDeltaTime);
        }
        {
            ZoneScopedN("CalculateCameraMatrices");
            Systems::CalculateCameraMatrices::Update(gameRegistry, clampedDeltaTime);
        }
        {
            ZoneScopedN("CalculateShadowCameraMatrices");
            Systems::CalculateShadowCameraMatrices::Update(gameRegistry, clampedDeltaTime);
        }

        {
            ZoneScopedN("UpdateSkyboxes");
            Systems::UpdateSkyboxes::Update(gameRegistry, clampedDeltaTime);
        }
        {
            ZoneScopedN("UpdateAreaLights");
            Systems::UpdateAreaLights::Update(gameRegistry, clampedDeltaTime);
        }
        {
            ZoneScopedN("CalculateTransformMatrices");
            Systems::CalculateTransformMatrices::Update(gameRegistry, clampedDeltaTime);
        }
        {
            ZoneScopedN("UpdateAABBs");
            Systems::UpdateAABBs::Update(gameRegistry, clampedDeltaTime);
        }
        {
            ZoneScopedN("UpdatePhysics");
            Systems::UpdatePhysics::Update(gameRegistry, clampedDeltaTime);
        }

        // Note: For now UpdateScripts should always be run last
        {
            ZoneScopedN("UpdateScripts");
            Systems::UpdateScripts::Update(gameRegistry, clampedDeltaTime);
        }

        if (joltState.updateTimer >= Singletons::JoltState::FixedDeltaTime)
        {
            joltState.updateTimer -= Singletons::JoltState::FixedDeltaTime;
        }

        // UI
        entt::registry& uiRegistry = *registries.uiRegistry;

        Systems::UI::UpdateBoundingRects::Update(uiRegistry, clampedDeltaTime);
        Systems::UI::HandleInput::Update(uiRegistry, clampedDeltaTime);
    }
}