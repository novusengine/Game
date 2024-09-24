#include "DrawDebugMesh.h"

#include "Game-Lib/ECS/Components/DebugRenderTransform.h"
#include "Game-Lib/ECS/Util/Transforms.h"
#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Rendering/Debug/DebugRenderer.h"
#include "Game-Lib/Util/ServiceLocator.h"

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
            obb.rotation = transform.GetLocalRotation();
            obb.extents = transform.GetLocalScale() * 0.5f;

            debugRenderer->DrawOBBSolid3D(obb.center, obb.extents, obb.rotation, debugMesh.color);
        });
    }
}