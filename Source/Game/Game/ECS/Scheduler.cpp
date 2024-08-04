#include "Scheduler.h"

#include "Game/ECS/Singletons/ActiveCamera.h"
#include "Game/ECS/Singletons/AreaLightInfo.h"
#include "Game/ECS/Singletons/CharacterSingleton.h"
#include "Game/ECS/Singletons/DayNightCycle.h"
#include "Game/ECS/Singletons/EngineStats.h"
#include "Game/ECS/Singletons/JoltState.h"
#include "Game/ECS/Singletons/RenderState.h"
#include "Game/ECS/Components/Camera.h"

#include "Game/ECS/Systems/UpdateAreaLights.h"
#include "Game/ECS/Systems/CalculateCameraMatrices.h"
#include "Game/ECS/Systems/CalculateShadowCameraMatrices.h"
#include "Game/ECS/Systems/UpdateDayNightCycle.h"
#include "Game/ECS/Systems/DrawDebugMesh.h"
#include "Game/ECS/Systems/FreeflyingCamera.h"
#include "Game/ECS/Systems/OrbitalCamera.h"
#include "Game/ECS/Systems/NetworkConnection.h"
#include "Game/ECS/Systems/UpdateNetworkedEntity.h"
#include "Game/ECS/Systems/UpdatePhysics.h"
#include "Game/ECS/Systems/UpdateScripts.h"
#include <Game/ECS/Systems/UpdateSkyboxes.h>
#include "Game/ECS/Systems/CalculateTransformMatrices.h"
#include "Game/ECS/Systems/UpdateAABBs.h"
#include "Game/ECS/Systems/CharacterController.h"

#include <Renderer/RenderSettings.h>

#include <entt/entt.hpp>

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

        Systems::UpdateDayNightCycle::Update(registry, clampedDeltaTime);
        Systems::NetworkConnection::Update(registry, clampedDeltaTime);
        Systems::DrawDebugMesh::Update(registry, clampedDeltaTime);

        Systems::CharacterController::Update(registry, clampedDeltaTime);
        Systems::UpdateNetworkedEntity::Update(registry, clampedDeltaTime);

        Systems::FreeflyingCamera::Update(registry, clampedDeltaTime);
        Systems::OrbitalCamera::Update(registry, clampedDeltaTime);
        Systems::CalculateCameraMatrices::Update(registry, clampedDeltaTime);
        Systems::CalculateShadowCameraMatrices::Update(registry, clampedDeltaTime);

        Systems::UpdateSkyboxes::Update(registry, clampedDeltaTime);
        Systems::UpdateAreaLights::Update(registry, clampedDeltaTime);
        Systems::CalculateTransformMatrices::Update(registry, clampedDeltaTime);
        Systems::UpdateAABBs::Update(registry, clampedDeltaTime);
        Systems::UpdatePhysics::Update(registry, clampedDeltaTime);

        // Note: For now UpdateScripts should always be run last
        Systems::UpdateScripts::Update(registry, clampedDeltaTime);

        if (joltState.updateTimer >= Singletons::JoltState::FixedDeltaTime)
        {
            joltState.updateTimer -= Singletons::JoltState::FixedDeltaTime;
        }
    }
}