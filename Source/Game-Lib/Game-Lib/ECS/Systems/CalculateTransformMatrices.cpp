#include "CalculateTransformMatrices.h"

#include "Game/ECS/Util/Transforms.h"
#include "Game/ECS/Singletons/RenderState.h"

#include <entt/entt.hpp>
#include "Game/Util/ServiceLocator.h"

namespace ECS::Systems
{
    void CalculateTransformMatrices::Update(entt::registry& registry, f32 deltaTime)
    {
        ECS::Singletons::RenderState& renderState = registry.ctx().get<ECS::Singletons::RenderState>();
        ECS::TransformSystem& transformQueue = ECS::TransformSystem::Get(registry);
        //convert the async transform queue into dirty transform components
        transformQueue.ProcessMovedEntities([&](entt::entity entity)
        {
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
    }
}