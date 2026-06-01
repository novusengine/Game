#include "UpdateBoundingRects.h"

#include "Game-Lib/ECS/Components/UI/BoundingRect.h"
#include "Game-Lib/ECS/Components/UI/Clipper.h"
#include "Game-Lib/ECS/Components/UI/Widget.h"
#include "Game-Lib/ECS/Util/Transform2D.h"
#include "Game-Lib/ECS/Util/UIUtil.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <entt/entt.hpp>
#include <tracy/Tracy.hpp>

namespace ECS::Systems::UI
{
    // Recompute one widget's subtree bound = its rect unioned with every direct child's subtree bound.
    // A clipChildren widget clamps to its clip rect (descendants can't draw/hit outside it), which
    // also stops upward propagation -- so widgets scrolling inside a clip viewport never dirty ancestors.
    static void RecomputeSubtreeBound(entt::registry& registry, ECS::Transform2DSystem& transform2DSystem, entt::entity entity, Components::UI::BoundingRect& rect)
    {
        auto* clipper = registry.try_get<Components::UI::Clipper>(entity);
        if (clipper != nullptr && clipper->clipChildren)
        {
            vec2 size = rect.max - rect.min;
            rect.subtreeMin = rect.min + size * clipper->clipRegionMin;
            rect.subtreeMax = rect.min + size * clipper->clipRegionMax;
            return;
        }

        vec2 mn = rect.min;
        vec2 mx = rect.max;
        transform2DSystem.IterateChildren(entity, [&](entt::entity child)
        {
            if (auto* childRect = registry.try_get<Components::UI::BoundingRect>(child))
            {
                mn = glm::min(mn, childRect->subtreeMin);
                mx = glm::max(mx, childRect->subtreeMax);
            }
        });
        rect.subtreeMin = mn;
        rect.subtreeMax = mx;
    }

    void UpdateBoundingRects::Update(entt::registry& registry, f32 deltaTime)
    {
        ZoneScopedN("UI::UpdateBoundingRects::Update");
        auto& transform2DSystem = ECS::Transform2DSystem::Get(registry);

        // Dirty transforms
        transform2DSystem.ProcessMovedEntities([&](entt::entity entity)
        {
            if (!registry.valid(entity))
                return;

            auto& transform = registry.get<Components::Transform2D>(entity);
            auto* rect = registry.try_get<Components::UI::BoundingRect>(entity);

            if (rect == nullptr)
            {
                return;
            }

            vec2 pos = transform.GetWorldPosition();
            vec2 size = transform.GetSize();

            rect->min = pos;
            rect->max = pos + size;

            registry.get_or_emplace<Components::UI::DirtyWidgetTransform>(entity);

            // If this widget is a clip source (owns a slot in _widgetClipRects or
            // _widgetMaskInfo), re-upload the slot from the new BoundingRect. The
            // descendants' draw-data indexes are stable; only the source's slot value
            // changes — one upload, not N.
            auto* clipper = registry.try_get<Components::UI::Clipper>(entity);
            if (clipper != nullptr && (clipper->clipRectBufferIndex != 0 || clipper->maskBufferIndex != 0))
                ECS::Util::UI::RecomputeClipSlots(&registry, entity);

            // Subtree bounds are maintained (and consumed) only for non-clipped widgets; everything
            // inside a clip region is broad-phased by its clip source. Scrolling moves only clipped
            // widgets, so skipping them here keeps it out of this O(siblings) up-walk.
            bool isClipped = clipper != nullptr && clipper->clipRegionOverrideEntity != entt::null;
            entt::entity cur = isClipped ? entt::null : entity;
            while (cur != entt::null && registry.valid(cur))
            {
                auto* curRect = registry.try_get<Components::UI::BoundingRect>(cur);
                if (curRect == nullptr)
                    break;

                vec2 oldMin = curRect->subtreeMin;
                vec2 oldMax = curRect->subtreeMax;
                RecomputeSubtreeBound(registry, transform2DSystem, cur, *curRect);
                if (curRect->subtreeMin == oldMin && curRect->subtreeMax == oldMax)
                    break;

                auto& curTransform = registry.get<Components::Transform2D>(cur);
                auto* node = curTransform.ownerNode;
                auto* parentNode = node ? node->GetParent() : nullptr;
                cur = parentNode ? parentNode->GetOwner() : entt::null;
            }
        });
    }
}