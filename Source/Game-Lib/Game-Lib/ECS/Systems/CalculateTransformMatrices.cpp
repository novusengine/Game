#include "CalculateTransformMatrices.h"

#include "Game-Lib/ECS/Util/Transforms.h"
#include "Game-Lib/ECS/Singletons/RenderState.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <entt/entt.hpp>
#include <tracy/Tracy.hpp>

namespace ECS::Systems
{
    void CalculateTransformMatrices::Update(entt::registry& registry, f32 deltaTime)
    {
        ZoneScopedN("ECS::CalculateTransformMatrices");

        ECS::Singletons::RenderState& renderState = registry.ctx().get<ECS::Singletons::RenderState>();
        ECS::TransformSystem& transformQueue = ECS::TransformSystem::Get(registry);
        //convert the async transform queue into dirty transform components
        transformQueue.ProcessMovedEntities([&](entt::entity entity)
        {
            if (!registry.valid(entity))
                return;

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