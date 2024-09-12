#include "UpdateBoundingRects.h"

#include "Game-Lib/ECS/Components/UI/BoundingRect.h"
#include "Game-Lib/ECS/Components/UI/Widget.h"
#include "Game-Lib/ECS/Util/Transform2D.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <entt/entt.hpp>

namespace ECS::Systems::UI
{
    void UpdateBoundingRects::Update(entt::registry& registry, f32 deltaTime)
    {
        registry.clear<Components::UI::DirtyWidgetTransform>();

        auto& transform2DSystem = ECS::Transform2DSystem::Get(registry);

        // Dirty transforms
        transform2DSystem.ProcessMovedEntities([&](entt::entity entity)
        {
            auto& transform = registry.get<Components::Transform2D>(entity);
            auto& rect = registry.get<Components::UI::BoundingRect>(entity);

            vec2 pos = transform.GetWorldPosition();
            vec2 size = transform.GetSize();

            rect.min = pos;
            rect.max = pos + size;

            registry.get_or_emplace<Components::UI::DirtyWidgetTransform>(entity);
        });
    }
}