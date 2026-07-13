#include "UpdateBoundingRects.h"

#include "Game-Lib/ECS/Components/UI/BoundingRect.h"
#include "Game-Lib/ECS/Components/UI/Canvas.h"
#include "Game-Lib/ECS/Components/UI/Clipper.h"
#include "Game-Lib/ECS/Components/UI/Widget.h"
#include "Game-Lib/ECS/Singletons/UISingleton.h"
#include "Game-Lib/ECS/Util/Transform2D.h"
#include "Game-Lib/ECS/Util/UIUtil.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <entt/entt.hpp>
#include <tracy/Tracy.hpp>

namespace ECS::Systems::UI
{
    void UpdateBoundingRects::Update(entt::registry& registry, f32 deltaTime)
    {
        ZoneScopedN("UI::UpdateBoundingRects::Update");
        auto& transform2DSystem = ECS::Transform2DSystem::Get(registry);
        auto& uiSingleton = registry.ctx().get<ECS::Singletons::UISingleton>();

        // Entities that moved this frame. Only the moved entity's own world rect is updated -- a container
        // move no longer recomputes its descendants (the GPU chain recomposes them, and hover composes
        // their rects on demand). We keep the set so a clip source whose ancestor moved can be re-derived.
        robin_hood::unordered_set<entt::entity> movedEntities;

        transform2DSystem.ProcessMovedEntities([&](entt::entity entity)
        {
            if (!registry.valid(entity))
                return;

            movedEntities.insert(entity);

            // Every chain participant re-uploads its matrix on move -- including rect-less containers, whose
            // slot anchors their children's GPU chain. The transform pass early-outs for canvas/3D widgets.
            registry.get_or_emplace<Components::UI::DirtyWidgetTransform>(entity);

            // Canvases do not own GPU matrix slots: top-level widgets bake the
            // canvas world transform into their root matrix. Consequently a
            // moved canvas must invalidate its direct children even though an
            // ordinary moved widget does not need descendant propagation.
            if (registry.all_of<Components::UI::Canvas>(entity))
            {
                transform2DSystem.IterateChildren(entity, [&registry](entt::entity childEntity)
                {
                    registry.get_or_emplace<Components::UI::DirtyWidgetTransform>(childEntity);
                });
            }

            auto& transform = registry.get<Components::Transform2D>(entity);

            auto* rect = registry.try_get<Components::UI::BoundingRect>(entity);
            if (rect != nullptr)
            {
                vec2 pos = transform.ComputeWorldTranslation();
                vec2 size = transform.GetSize();

                // A size change alters the local vertices (quad / glyph layout), so re-bake them; a
                // position-only move leaves the local verts untouched (only the screen matrix re-uploads).
                if (size != (rect->max - rect->min))
                    registry.get_or_emplace<Components::UI::DirtyWidgetData>(entity);

                rect->min = pos;
                rect->max = pos + size;
            }

            // A moved clip source re-uploads its own slot from the new rect.
            auto* clipper = registry.try_get<Components::UI::Clipper>(entity);
            if (clipper != nullptr && (clipper->clipRectBufferIndex != 0 || clipper->maskBufferIndex != 0))
                ECS::Util::UI::RecomputeClipSlots(&registry, entity);
        });

        if (movedEntities.empty())
            return;

        // A clip source whose ANCESTOR moved (but which wasn't itself moved) now has a stale world rect,
        // since descendant rects are no longer propagated. Clip sources are few, so iterate them and
        // re-derive the ones an ancestor moved. Stale/destroyed entries are dropped lazily.
        std::vector<entt::entity> deadSources;
        for (entt::entity sourceEntity : uiSingleton.clipSourceEntities)
        {
            auto* clipper = registry.valid(sourceEntity) ? registry.try_get<Components::UI::Clipper>(sourceEntity) : nullptr;
            if (clipper == nullptr || (clipper->clipRectBufferIndex == 0 && clipper->maskBufferIndex == 0))
            {
                deadSources.push_back(sourceEntity); // no longer a clip source / destroyed
                continue;
            }

            if (movedEntities.count(sourceEntity) != 0)
                continue; // already re-derived in the moved loop above

            auto& transform = registry.get<Components::Transform2D>(sourceEntity);
            auto* node = transform.ownerNode;
            auto* parentNode = node ? node->GetParent() : nullptr;

            bool ancestorMoved = false;
            while (parentNode != nullptr)
            {
                if (movedEntities.count(parentNode->GetOwner()) != 0)
                {
                    ancestorMoved = true;
                    break;
                }
                parentNode = parentNode->GetParent();
            }

            if (!ancestorMoved)
                continue;

            if (auto* rect = registry.try_get<Components::UI::BoundingRect>(sourceEntity))
            {
                vec2 pos = transform.ComputeWorldTranslation();
                rect->min = pos;
                rect->max = pos + transform.GetSize();
            }
            ECS::Util::UI::RecomputeClipSlots(&registry, sourceEntity);
        }

        for (entt::entity dead : deadSources)
            uiSingleton.clipSourceEntities.erase(dead);
    }
}
