#include "DrawDebugMesh.h"

#include "Game/ECS/Components/DebugRenderTransform.h"
#include "Game/ECS/Components/Transform.h"
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
		DebugRenderer* debugRenderer = ServiceLocator::GetGameRenderer()->GetDebugRenderer();

		auto view = registry.view<const Components::Transform, const Components::DebugRenderTransform>();
		if (view.size_hint() == 0)
			return;

		view.each([&](entt::entity entity, const Components::Transform& transform, const Components::DebugRenderTransform& debugMesh)
		{
			Geometry::OrientedBoundingBox obb;
			obb.center = transform.position;
			obb.rotation = transform.rotation;
			obb.extents = transform.scale * 0.5f;

			u32 color = static_cast<u32>(debugMesh.color.r * 255.0f); // Red
			color |= static_cast<u32>(debugMesh.color.g * 255.0f) << 8; // Green
			color |= static_cast<u32>(debugMesh.color.b * 255.0f) << 16; // Blue
			color |= 1u << 24u; // Alpha

			debugRenderer->DrawOBB3D(obb.center, obb.extents, obb.rotation, color);
		});
	}
}