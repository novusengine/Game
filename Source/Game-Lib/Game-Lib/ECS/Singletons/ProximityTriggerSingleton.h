#pragma once
#include <Base/Types.h>

#include <entt/entity/fwd.hpp>
#include <RTree/RTree.h>
#include <robinhood/robinhood.h>

namespace ECS::Singletons
{
    struct ProximityTriggerSingleton
    {
    public:
        RTree<entt::entity, f32, 3> proximityTriggers;

        robin_hood::unordered_map<entt::entity, robin_hood::unordered_set<entt::entity>> entityToProximityTriggers; // Maps entity to the triggers it is currently inside
        robin_hood::unordered_map<u32, entt::entity> triggerIDToEntity; // Maps trigger ID to entity
    };
}