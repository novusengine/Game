#include "Scheduler.h"

#include <Renderer/RenderSettings.h>

#include <Game/ECS/Singletons/ActiveCamera.h>
#include <Game/ECS/Singletons/EngineStats.h>
#include <Game/ECS/Components/Camera.h>
#include <Game/ECS/Components/Transform.h>
#include <Game/ECS/Systems/NetworkConnection.h>
#include <Game/ECS/Systems/FreeflyingCamera.h>
#include <Game/ECS/Systems/CalculateCameraMatrices.h>
#include <Game/ECS/Systems/UpdateScripts.h>

#include <entt/entt.hpp>

namespace ECS
{
	Scheduler::Scheduler()
	{

	}

	void Scheduler::Init(entt::registry& registry)
	{
		Systems::NetworkConnection::Init(registry);
		Systems::FreeflyingCamera::Init(registry);
		Systems::UpdateScripts::Init(registry);

		entt::registry::context& ctx = registry.ctx();

		// Temporarily create a camera here for debugging
		{
			Singletons::ActiveCamera& activeCamera = ctx.emplace<Singletons::ActiveCamera>();

			entt::entity cameraEntity = registry.create();
			activeCamera.entity = cameraEntity;
			Components::Transform& transform = registry.emplace<Components::Transform>(cameraEntity);
			transform.position = vec3(0, 10, -10);

			Components::Camera& camera = registry.emplace<Components::Camera>(cameraEntity);
			camera.aspectRatio = static_cast<f32>(Renderer::Settings::SCREEN_WIDTH) / static_cast<f32>(Renderer::Settings::SCREEN_HEIGHT);
			camera.pitch = 30.0f;
		}

		Singletons::EngineStats& engineStats = ctx.emplace<Singletons::EngineStats>();
	}

	void Scheduler::Update(entt::registry& registry, f32 deltaTime)
	{
		// TODO: You know, actually scheduling stuff and multithreading (enkiTS tasks?)
		Systems::NetworkConnection::Update(registry, deltaTime);
		Systems::FreeflyingCamera::Update(registry, deltaTime);
		Systems::CalculateCameraMatrices::Update(registry, deltaTime);
		Systems::UpdateScripts::Update(registry, deltaTime);
	}
}