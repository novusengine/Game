#include "Scheduler.h"

#include "Game/Application/EnttRegistries.h"
#include "Game/ECS/Components/Camera.h"
#include "Game/ECS/Singletons/ActiveCamera.h"
#include "Game/ECS/Singletons/AreaLightInfo.h"
#include "Game/ECS/Singletons/CharacterSingleton.h"
#include "Game/ECS/Singletons/DayNightCycle.h"
#include "Game/ECS/Singletons/EngineStats.h"
#include "Game/ECS/Singletons/JoltState.h"
#include "Game/ECS/Singletons/RenderState.h"
#include "Game/ECS/Systems/CalculateCameraMatrices.h"
#include "Game/ECS/Systems/CalculateShadowCameraMatrices.h"
#include "Game/ECS/Systems/CalculateTransformMatrices.h"
#include "Game/ECS/Systems/CharacterController.h"
#include "Game/ECS/Systems/DrawDebugMesh.h"
#include "Game/ECS/Systems/FreeflyingCamera.h"
#include "Game/ECS/Systems/NetworkConnection.h"
#include "Game/ECS/Systems/OrbitalCamera.h"
#include "Game/ECS/Systems/UI/HandleInput.h"
#include "Game/ECS/Systems/UI/UpdateBoundingRects.h"
#include "Game/ECS/Systems/UpdateAABBs.h"
#include "Game/ECS/Systems/UpdateAreaLights.h"
#include "Game/ECS/Systems/UpdateDayNightCycle.h"
#include "Game/ECS/Systems/UpdateNetworkedEntity.h"
#include "Game/ECS/Systems/UpdatePhysics.h"
#include "Game/ECS/Systems/UpdateScripts.h"
#include "Game/ECS/Systems/UpdateSkyboxes.h"

#include <Renderer/RenderSettings.h>

#include <entt/entt.hpp>

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

        entt::registry::context& gameCtx = gameRegistry.ctx();
        gameCtx.emplace<Singletons::EngineStats>();
        gameCtx.emplace<Singletons::RenderState>();

        // UI
        entt::registry& uiRegistry = *registries.uiRegistry;

        Systems::UI::HandleInput::Init(uiRegistry);
    }

    void Scheduler::Update(EnttRegistries& registries, f32 deltaTime)
    {
        // Game
        entt::registry& gameRegistry = *registries.gameRegistry;

        // TODO: You know, actually scheduling stuff and multithreading (enkiTS tasks?)
        entt::registry::context& gameCtx = gameRegistry.ctx();
        auto& joltState = gameCtx.get<Singletons::JoltState>();

        static constexpr f32 maxDeltaTimeDiff = 1.0f / 60.0f;
        f32 clampedDeltaTime = glm::clamp(deltaTime, 0.0f, maxDeltaTimeDiff);

        joltState.updateTimer += glm::clamp(clampedDeltaTime, 0.0f, Singletons::JoltState::FixedDeltaTime);

        Systems::UpdateDayNightCycle::Update(gameRegistry, clampedDeltaTime);
        Systems::NetworkConnection::Update(gameRegistry, clampedDeltaTime);
        Systems::DrawDebugMesh::Update(gameRegistry, clampedDeltaTime);

        Systems::CharacterController::Update(gameRegistry, clampedDeltaTime);
        Systems::UpdateNetworkedEntity::Update(gameRegistry, clampedDeltaTime);

        Systems::FreeflyingCamera::Update(gameRegistry, clampedDeltaTime);
        Systems::OrbitalCamera::Update(gameRegistry, clampedDeltaTime);
        Systems::CalculateCameraMatrices::Update(gameRegistry, clampedDeltaTime);
        Systems::CalculateShadowCameraMatrices::Update(gameRegistry, clampedDeltaTime);

        Systems::UpdateSkyboxes::Update(gameRegistry, clampedDeltaTime);
        Systems::UpdateAreaLights::Update(gameRegistry, clampedDeltaTime);
        Systems::CalculateTransformMatrices::Update(gameRegistry, clampedDeltaTime);
        Systems::UpdateAABBs::Update(gameRegistry, clampedDeltaTime);
        Systems::UpdatePhysics::Update(gameRegistry, clampedDeltaTime);

        // Note: For now UpdateScripts should always be run last
        Systems::UpdateScripts::Update(gameRegistry, clampedDeltaTime);

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