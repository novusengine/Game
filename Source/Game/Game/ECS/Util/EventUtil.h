#pragma once
#include "Game/Application/EnttRegistries.h"
#include "Game/Util/ServiceLocator.h"

#include <Base/Types.h>

#include <entt/entt.hpp>

namespace ECS::Util
{
    namespace EventUtil
    {
        template <typename... Events>
        inline void PushEventTo(entt::registry& registry, Events&&... events)
        {
            entt::entity eventEntity = registry.create();
            (registry.emplace<std::decay_t<Events>>(eventEntity, std::forward<Events>(events)), ...);
        }

        template <typename... Events>
        inline void PushEvent(Events&&... events)
        {
            EnttRegistries* registries = ServiceLocator::GetEnttRegistries();
            entt::registry* eventRegistry = registries->eventOutgoingRegistry;

            PushEventTo(*eventRegistry, std::forward<Events>(events)...);
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