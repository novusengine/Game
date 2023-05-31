#include "CalculateTransformMatrices.h"

#include "Game/ECS/Components/Transform.h"
#include "Game/ECS/Singletons/RenderState.h"

#include <entt/entt.hpp>

namespace ECS::Systems
{
    std::unordered_set<entt::entity> CalculateTransformMatrices::_entitiesToUndirty;

	void CalculateTransformMatrices::Update(entt::registry& registry, f32 deltaTime)
	{
        ECS::Singletons::RenderState& renderState = registry.ctx().at<ECS::Singletons::RenderState>();

        auto view = registry.view<Components::Transform, Components::DirtyTransform>();
        view.each([&](entt::entity entity, Components::Transform& transform, ECS::Components::DirtyTransform& dirtyTransform)
        {
            if (transform.isDirty)
            {
                mat4x4 rotationMatrix = glm::toMat4(transform.rotation);
                mat4x4 scaleMatrix = glm::scale(mat4x4(1.0f), transform.scale);
                transform.matrix = glm::translate(mat4x4(1.0f), transform.position) * rotationMatrix * scaleMatrix;

                dirtyTransform.dirtyFrame = renderState.frameNumber;

                transform.isDirty = false;
            }

            // Delay removing the dirty component until the next frame
            _entitiesToUndirty.insert(entity);
        });

        // Remove dirty components from last frame
        auto it = _entitiesToUndirty.begin();
        while (it != _entitiesToUndirty.end())
        {
            entt::entity entity = *it;
            // make sure the entity is still valid
            if (!registry.valid(entity))
            {
                it = _entitiesToUndirty.erase(it);
				continue;
            }

            if (!registry.all_of<Components::DirtyTransform>(entity))
            {
                it = _entitiesToUndirty.erase(it);
                continue;
            }

            Components::DirtyTransform& dirtyTransform = registry.get<Components::DirtyTransform>(entity);
            if (dirtyTransform.dirtyFrame != renderState.frameNumber)
            {
                registry.remove<Components::DirtyTransform>(entity); // then remove the component from the entity in the registry
                it = _entitiesToUndirty.erase(it); // erase the entity from _entitiesToUndirty first
            }
            else
            {
                it++;
            }
            
        }
	}
}