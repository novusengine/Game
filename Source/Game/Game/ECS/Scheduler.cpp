#include "Scheduler.h"

#include <Renderer/RenderSettings.h>

#include <Game/ECS/Singletons/ActiveCamera.h>
#include <Game/ECS/Singletons/EngineStats.h>
#include <Game/ECS/Singletons/RenderState.h>
#include <Game/ECS/Components/Camera.h>

#include <Game/ECS/Systems/CalculateCameraMatrices.h>
#include <Game/ECS/Systems/DrawDebugMesh.h>
#include <Game/ECS/Systems/FreeflyingCamera.h>
#include <Game/ECS/Systems/NetworkConnection.h>
#include <Game/ECS/Systems/UpdatePhysics.h>
#include <Game/ECS/Systems/UpdateScripts.h>
#include <Game/ECS/Systems/CalculateTransformMatrices.h>
#include <Game/ECS/Systems/UpdateAABBs.h>

#include <entt/entt.hpp>

namespace ECS
{
	Scheduler::Scheduler()
	{

	}

	void Scheduler::Init(entt::registry& registry)
	{
		Systems::NetworkConnection::Init(registry);
		Systems::UpdatePhysics::Init(registry);
		Systems::DrawDebugMesh::Init(registry);
		Systems::FreeflyingCamera::Init(registry);
		Systems::UpdateScripts::Init(registry);

		entt::registry::context& ctx = registry.ctx();
		Singletons::EngineStats& engineStats = ctx.emplace<Singletons::EngineStats>();
		
		ctx.emplace<Singletons::RenderState>();
	}

	void Scheduler::Update(entt::registry& registry, f32 deltaTime)
	{
		// TODO: You know, actually scheduling stuff and multithreading (enkiTS tasks?)
		Systems::CalculateTransformMatrices::Update(registry, deltaTime);
		Systems::UpdateAABBs::Update(registry, deltaTime);
		Systems::NetworkConnection::Update(registry, deltaTime);
		Systems::UpdatePhysics::Update(registry, deltaTime);
		Systems::DrawDebugMesh::Update(registry, deltaTime);
		Systems::FreeflyingCamera::Update(registry, deltaTime);
		Systems::CalculateCameraMatrices::Update(registry, deltaTime);

		// Note: For now UpdateScripts should always be run last
		Systems::UpdateScripts::Update(registry, deltaTime);
	}
}