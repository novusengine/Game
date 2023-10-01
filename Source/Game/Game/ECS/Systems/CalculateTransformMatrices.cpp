#include "CalculateTransformMatrices.h"

#include "Game/ECS/Components/Transform.h"
#include "Game/ECS/Singletons/RenderState.h"

#include <entt/entt.hpp>
#include "Game/Util/ServiceLocator.h"

namespace ECS::Systems
{
    std::unordered_set<entt::entity> CalculateTransformMatrices::_entitiesToUndirty;

	void CalculateTransformMatrices::Update(entt::registry& registry, f32 deltaTime)
	{
        ECS::Singletons::RenderState& renderState = registry.ctx().at<ECS::Singletons::RenderState>();

        ECS::Components::DirtyTransformQueue* q = ServiceLocator::GetTransformQueue();
        q->ProcessQueue([&](entt::entity entity) {

            registry.get<Components::Transform>(entity).matrix = registry.get<Components::Transform>(entity).GetMatrix();
            registry.get_or_emplace<ECS::Components::DirtyTransform>(entity).dirtyFrame = renderState.frameNumber;
        });

        auto view = registry.view<Components::DirtyTransform>();
        view.each([&](entt::entity entity,  ECS::Components::DirtyTransform& dirtyTransform)
		{
            //delete the dirty components from entities that werent dirtied this frame
				if (dirtyTransform.dirtyFrame != renderState.frameNumber)
				{
                   registry.remove<Components::DirtyTransform>(entity);
				}
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