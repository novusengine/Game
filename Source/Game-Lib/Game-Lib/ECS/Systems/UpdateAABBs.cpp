#include "UpdateAABBs.h"

#include "Game-Lib/ECS/Components/AABB.h"
#include "Game-Lib/ECS/Components/Unit.h"
#include "Game-Lib/ECS/Singletons/NetworkState.h"
#include "Game-Lib/ECS/Util/Transforms.h"

#include <entt/entt.hpp>
#include <tracy/Tracy.hpp>

namespace ECS::Systems
{
    void UpdateWorldAABB(const Components::Transform& transform, const Components::AABB& aabb, Components::WorldAABB& worldAABB)
    {
        const mat4x4 transformMatrix = transform.GetMatrix();
        const vec3 worldCenter = vec3(transformMatrix * vec4(aabb.centerPos, 1.0f));
        const mat3x3 linearTransform = mat3x3(transformMatrix);
        const vec3 worldExtents =
            glm::abs(linearTransform[0]) * aabb.extents.x +
            glm::abs(linearTransform[1]) * aabb.extents.y +
            glm::abs(linearTransform[2]) * aabb.extents.z;

        worldAABB.min = worldCenter - worldExtents;
        worldAABB.max = worldCenter + worldExtents;
    }

    void UpdateNetworkVisTree(entt::registry& registry, entt::entity entity, const Components::WorldAABB& worldAABB)
    {
        const auto* unit = registry.try_get<Components::Unit>(entity);
        if (!unit || !unit->networkID.IsValid())
            return;

        auto& networkState = registry.ctx().get<Singletons::NetworkState>();
        if (!networkState.networkVisTree)
            return;

        networkState.networkVisTree->Remove(unit->networkID);
        networkState.networkVisTree->Insert(&worldAABB.min.x, &worldAABB.max.x, unit->networkID);
    }

    void UpdateAABBs::Update(entt::registry& registry, f32 deltaTime)
    {
        ZoneScopedN("ECS::UpdateAABBs");

        // Update AABBs for entities with dirty transforms
        auto dirtyTransformView = registry.view<Components::Transform, Components::AABB, Components::WorldAABB, Components::DirtyTransform>();
        dirtyTransformView.each([&](entt::entity entity, Components::Transform& transform, Components::AABB& aabb, Components::WorldAABB& worldAABB, ECS::Components::DirtyTransform& dirtyTransform)
        {
            UpdateWorldAABB(transform, aabb, worldAABB);
            UpdateNetworkVisTree(registry, entity, worldAABB);
        });

        // Update AABBs for entities with dirty AABBs
        auto dirtyAABBView = registry.view<Components::Transform, Components::AABB, Components::WorldAABB, Components::DirtyAABB>();
        dirtyAABBView.each([&](entt::entity entity, Components::Transform& transform, Components::AABB& aabb, Components::WorldAABB& worldAABB)
        {
            if (registry.all_of<Components::DirtyTransform>(entity))
                return;

            UpdateWorldAABB(transform, aabb, worldAABB);
            UpdateNetworkVisTree(registry, entity, worldAABB);
        });

        registry.clear<Components::DirtyAABB>();
    }
}
