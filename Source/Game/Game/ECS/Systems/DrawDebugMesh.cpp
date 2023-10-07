#include "DrawDebugMesh.h"

#include "Game/ECS/Components/DebugRenderTransform.h"
#include "Game/ECS/Util/Transforms.h"
#include "Game/Rendering/GameRenderer.h"
#include "Game/Rendering/Debug/DebugRenderer.h"
#include "Game/Util/ServiceLocator.h"

#include <entt/entt.hpp>

namespace ECS::Systems
{
	void DrawDebugMesh::Init(entt::registry& registry)
	{
	}

	void DrawDebugMesh::Update(entt::registry& registry, f32 deltaTime)
	{
		auto view = registry.view<const Components::Transform, const Components::DebugRenderTransform>();
		if (view.size_hint() == 0)
			return;

		DebugRenderer* debugRenderer = ServiceLocator::GetGameRenderer()->GetDebugRenderer();

		view.each([&](entt::entity entity, const Components::Transform& transform, const Components::DebugRenderTransform& debugMesh)
		{
			Geometry::OrientedBoundingBox obb;
			obb.center = transform.GetWorldPosition();
			obb.rotation = transform.GetPosition();
			obb.extents = transform.GetScale() * 0.5f;

			debugRenderer->DrawOBB3D(obb.center, obb.extents, obb.rotation, debugMesh.color);
		});
	}
}