#include "UpdateAABBs.h"

#include "Game-Lib/ECS/Components/AABB.h"
#include "Game-Lib/ECS/Util/Transforms.h"

#include <entt/entt.hpp>
#include <tracy/Tracy.hpp>

namespace ECS::Systems
{
    void UpdateWorldAABB(const Components::Transform& transform, const Components::AABB& aabb, Components::WorldAABB& worldAABB)
    {
        // Calculate the world AABB
        glm::vec3 min = aabb.centerPos - aabb.extents;
        glm::vec3 max = aabb.centerPos + aabb.extents;

        glm::vec3 corners[8] = {
            glm::vec3(min.x, min.y, min.z),
            glm::vec3(min.x, min.y, max.z),
            glm::vec3(min.x, max.y, min.z),
            glm::vec3(min.x, max.y, max.z),
            glm::vec3(max.x, min.y, min.z),
            glm::vec3(max.x, min.y, max.z),
            glm::vec3(max.x, max.y, min.z),
            glm::vec3(max.x, max.y, max.z)
        };

        const mat4x4 transformMatrix = transform.GetMatrix();
        for (i32 i = 0; i < 8; ++i)
        {
            corners[i] = transformMatrix * glm::vec4(corners[i], 1.0f);
        }

        worldAABB.min = vec3(1000000000.0f);
        worldAABB.max = vec3(-1000000000.0f);

        for (i32 i = 1; i < 8; ++i)
        {
            worldAABB.min = glm::min(worldAABB.min, corners[i]);
            worldAABB.max = glm::max(worldAABB.max, corners[i]);
        }
    }

    void UpdateAABBs::Update(entt::registry& registry, f32 deltaTime)
    {
        ZoneScopedN("ECS::UpdateAABBs");

        // Update AABBs for entities with dirty transforms
        auto dirtyTransformView = registry.view<Components::Transform, Components::AABB, Components::WorldAABB, Components::DirtyTransform>();
        dirtyTransformView.each([&](entt::entity entity, Components::Transform& transform, Components::AABB& aabb, Components::WorldAABB& worldAABB, ECS::Components::DirtyTransform& dirtyTransform)
        {
            UpdateWorldAABB(transform, aabb, worldAABB);
        });

        // Update AABBs for entities with dirty AABBs
        auto dirtyAABBView = registry.view<Components::Transform, Components::AABB, Components::WorldAABB, Components::DirtyAABB>();
        dirtyAABBView.each([&](entt::entity entity, Components::Transform& transform, Components::AABB& aabb, Components::WorldAABB& worldAABB)
        {
            UpdateWorldAABB(transform, aabb, worldAABB);
        });
    }
}