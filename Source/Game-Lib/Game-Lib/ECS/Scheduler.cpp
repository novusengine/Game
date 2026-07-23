#include "Scheduler.h"

#include "Game-Lib/ECS/Components/Camera.h"
#include "Game-Lib/ECS/Singletons/ActiveCamera.h"
#include "Game-Lib/ECS/Singletons/AreaLightInfo.h"
#include "Game-Lib/ECS/Singletons/CharacterSingleton.h"
#include "Game-Lib/ECS/Singletons/DayNightCycle.h"
#include "Game-Lib/ECS/Singletons/EditorSelection.h"
#include "Game-Lib/ECS/Singletons/EngineStats.h"
#include "Game-Lib/ECS/Singletons/JoltState.h"
#include "Game-Lib/ECS/Singletons/ProximityTriggerSingleton.h"
#include "Game-Lib/ECS/Singletons/RenderState.h"
#include "Game-Lib/ECS/Systems/Animation.h"
#include "Game-Lib/ECS/Systems/UpdateAreaLights.h"
#include "Game-Lib/ECS/Systems/CalculateCameraMatrices.h"
#include "Game-Lib/ECS/Systems/CalculateTransformMatrices.h"
#include "Game-Lib/ECS/Systems/UpdateDayNightCycle.h"
#include "Game-Lib/ECS/Systems/DrawDebugMesh.h"
#include "Game-Lib/ECS/Systems/FreeflyingCamera.h"
#include "Game-Lib/ECS/Systems/OrbitalCamera.h"
#include "Game-Lib/ECS/Systems/NetworkConnection.h"
#include "Game-Lib/ECS/Systems/UpdateProximityTriggers.h"
#include "Game-Lib/ECS/Systems/UpdateUnitEntities.h"
#include "Game-Lib/ECS/Systems/UpdatePhysics.h"
#include "Game-Lib/ECS/Systems/UpdateScripts.h"
#include "Game-Lib/ECS/Systems/UpdateSkyboxes.h"
#include "Game-Lib/ECS/Systems/UpdateAABBs.h"
#include "Game-Lib/ECS/Systems/CharacterController.h"
#include "Game-Lib/ECS/Systems/CharacterControllerInput.h"
#include "Game-Lib/ECS/Systems/Editor/EditorTools.h"
#include "Game-Lib/ECS/Systems/UI/HandleInput.h"
#include "Game-Lib/ECS/Systems/UI/UpdateBoundingRects.h"
#include "Game-Lib/ECS/Util/EventUtil.h"
#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Rendering/Model/ModelLoader.h"

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
        NC_LOG_INFO("ECS Scheduler : Initializing");
        entt::registry& gameRegistry = *registries.gameRegistry;

        Systems::NetworkConnection::Init(gameRegistry);
        Systems::Animation::Init(gameRegistry);
        Systems::UpdateUnitEntities::Init(gameRegistry);
        Systems::UpdatePhysics::Init(gameRegistry);
        Systems::DrawDebugMesh::Init(gameRegistry);
        Systems::FreeflyingCamera::Init(gameRegistry);
        Systems::OrbitalCamera::Init(gameRegistry);
        Systems::CharacterController::Init(gameRegistry);
        Systems::UpdateScripts::Init(gameRegistry);
        Systems::UpdateDayNightCycle::Init(gameRegistry);
        Systems::UpdateAreaLights::Init(gameRegistry);
        Systems::UpdateSkyboxes::Init(gameRegistry);
        Systems::Editor::EditorTools::Init(gameRegistry);

        entt::registry::context& ctx = gameRegistry.ctx();
        ctx.emplace<Singletons::EngineStats>();
        ctx.emplace<Singletons::RenderState>();
        ctx.emplace<Singletons::ProximityTriggerSingleton>();
        ctx.emplace<Singletons::EditorSelection>();

        // UI
        entt::registry& uiRegistry = *registries.uiRegistry;

        Systems::UI::HandleInput::Init(uiRegistry);
        NC_LOG_INFO("ECS Scheduler : Initialized");
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

        // Day/night cycle is a wall-clock-style timer: pass the unclamped deltaTime so
        // the in-game time tracks real seconds 1:1. Clamping (as the rest of this
        // function uses for physics/animation stability) makes the cycle drift behind
        // wall-clock whenever the framerate dips below 60 FPS.
        Systems::UpdateDayNightCycle::Update(gameRegistry, deltaTime);
        Systems::NetworkConnection::Update(gameRegistry, clampedDeltaTime);
        Systems::DrawDebugMesh::Update(gameRegistry, clampedDeltaTime);
        Systems::Animation::Update(gameRegistry, clampedDeltaTime);
        Systems::CharacterController::Update(gameRegistry, clampedDeltaTime);
        Systems::CharacterControllerInput::UpdateAutoAttack(gameRegistry, clampedDeltaTime);
        Systems::UpdateUnitEntities::Update(gameRegistry, clampedDeltaTime);
        Systems::FreeflyingCamera::Update(gameRegistry, clampedDeltaTime);
        Systems::OrbitalCamera::Update(gameRegistry, clampedDeltaTime);
        Systems::CalculateCameraMatrices::Update(gameRegistry, clampedDeltaTime);
        Systems::CharacterControllerInput::UpdateHoveredUnit(gameRegistry, clampedDeltaTime);
        Systems::UpdateSkyboxes::Update(gameRegistry, clampedDeltaTime);
        Systems::UpdateAreaLights::Update(gameRegistry, clampedDeltaTime);
        Systems::Editor::EditorTools::Update(gameRegistry, clampedDeltaTime);
        Systems::CalculateTransformMatrices::Update(gameRegistry, clampedDeltaTime);
        Systems::UpdateAABBs::Update(gameRegistry, clampedDeltaTime);
        Systems::UpdatePhysics::Update(gameRegistry, clampedDeltaTime);
        Systems::UpdateProximityTriggers::Update(gameRegistry, clampedDeltaTime);

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
        Systems::UI::UpdateBoundingRects::Update(uiRegistry, clampedDeltaTime); // Run Twice to update any entities added during HandleInput::Update
    }
}
