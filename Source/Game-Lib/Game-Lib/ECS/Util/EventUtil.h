#pragma once
#include "Game-Lib/Application/EnttRegistries.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Base/Types.h>

#include <entt/entt.hpp>

namespace ECS::Util
{
    namespace EventUtil
    {
        template <typename... Events>
        inline void PushEventTo(entt::registry& registry, entt::entity entity, Events&&... events)
        {
            (registry.emplace<std::decay_t<Events>>(entity, std::forward<Events>(events)), ...);
        }

        template <typename... Events>
        inline void PushEvent(Events&&... events)
        {
            EnttRegistries* registries = ServiceLocator::GetEnttRegistries();
            entt::registry* eventRegistry = registries->eventOutgoingRegistry;

            entt::entity eventEntity = eventRegistry->create();
            PushEventTo(*eventRegistry, eventEntity, std::forward<Events>(events)...);
        }

        template <typename T, typename Handler>
        inline void OnEvent(Handler&& handler)
        {
            EnttRegistries* registries = ServiceLocator::GetEnttRegistries();
            entt::registry* eventRegistry = registries->eventIncomingRegistry;

            eventRegistry->view<T>().each(std::forward<Handler>(handler));
        }
    }
}