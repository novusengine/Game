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
#include <Game-Lib/ECS/Systems/UpdateSkyboxes.h>
#include "Game-Lib/ECS/Systems/CalculateTransformMatrices.h"
#include "Game-Lib/ECS/Systems/UpdateAABBs.h"
#include "Game-Lib/ECS/Systems/CharacterController.h"

#include <Renderer/RenderSettings.h>

#include <entt/entt.hpp>

#include <tracy/Tracy.hpp>

namespace ECS
{
    Scheduler::Scheduler()
    {

    }

    void Scheduler::Init(entt::registry& registry)
    {
        Systems::NetworkConnection::Init(registry);
        Systems::UpdateNetworkedEntity::Init(registry);
        Systems::UpdatePhysics::Init(registry);
        Systems::DrawDebugMesh::Init(registry);
        Systems::FreeflyingCamera::Init(registry);
        Systems::OrbitalCamera::Init(registry);
        Systems::CharacterController::Init(registry);
        Systems::UpdateScripts::Init(registry);
        Systems::UpdateDayNightCycle::Init(registry);
        Systems::UpdateAreaLights::Init(registry);
        Systems::UpdateSkyboxes::Init(registry);

        entt::registry::context& ctx = registry.ctx();
        ctx.emplace<Singletons::EngineStats>();
        ctx.emplace<Singletons::RenderState>();
    }

    void Scheduler::Update(entt::registry& registry, f32 deltaTime)
    {
        // TODO: You know, actually scheduling stuff and multithreading (enkiTS tasks?)
        entt::registry::context& ctx = registry.ctx();
        auto& joltState = ctx.get<Singletons::JoltState>();

        static constexpr f32 maxDeltaTimeDiff = 1.0f / 60.0f;
        f32 clampedDeltaTime = glm::clamp(deltaTime, 0.0f, maxDeltaTimeDiff);

        joltState.updateTimer += glm::clamp(clampedDeltaTime, 0.0f, Singletons::JoltState::FixedDeltaTime);

        {
            ZoneScopedN("UpdateDayNightCycle");
            Systems::UpdateDayNightCycle::Update(registry, clampedDeltaTime);
        }
        {
            ZoneScopedN("NetworkConnection");
            Systems::NetworkConnection::Update(registry, clampedDeltaTime);
        }
        {
            ZoneScopedN("DrawDebugMesh");
            Systems::DrawDebugMesh::Update(registry, clampedDeltaTime);
        }

        {
            ZoneScopedN("CharacterController");
            Systems::CharacterController::Update(registry, clampedDeltaTime);
        }
        {
            ZoneScopedN("UpdateNetworkedEntity");
            Systems::UpdateNetworkedEntity::Update(registry, clampedDeltaTime);
        }


        {
            ZoneScopedN("FreeflyingCamera");
            Systems::FreeflyingCamera::Update(registry, clampedDeltaTime);
        }
        {
            ZoneScopedN("OrbitalCamera");
            Systems::OrbitalCamera::Update(registry, clampedDeltaTime);
        }
        {
            ZoneScopedN("CalculateCameraMatrices");
            Systems::CalculateCameraMatrices::Update(registry, clampedDeltaTime);
        }
        {
            ZoneScopedN("CalculateShadowCameraMatrices");
            Systems::CalculateShadowCameraMatrices::Update(registry, clampedDeltaTime);
        }


        {
            ZoneScopedN("UpdateSkyboxes");
            Systems::UpdateSkyboxes::Update(registry, clampedDeltaTime);
        }
        {
            ZoneScopedN("UpdateAreaLights");
            Systems::UpdateAreaLights::Update(registry, clampedDeltaTime);
        }
        {
            ZoneScopedN("CalculateTransformMatrices");
            Systems::CalculateTransformMatrices::Update(registry, clampedDeltaTime);
        }
        {
            ZoneScopedN("UpdateAABBs");
            Systems::UpdateAABBs::Update(registry, clampedDeltaTime);
        }
        {
            ZoneScopedN("UpdatePhysics");
            Systems::UpdatePhysics::Update(registry, clampedDeltaTime);
        }


        // Note: For now UpdateScripts should always be run last
        {
            ZoneScopedN("UpdateScripts");
            Systems::UpdateScripts::Update(registry, clampedDeltaTime);
        }

        if (joltState.updateTimer >= Singletons::JoltState::FixedDeltaTime)
        {
            joltState.updateTimer -= Singletons::JoltState::FixedDeltaTime;
        }
    }
}