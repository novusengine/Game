#pragma once
#include <Base/Types.h>

#include <MetaGen/Shared/ProximityTrigger/ProximityTrigger.h>

#include <entt/entt.hpp>
#include <set>

namespace ECS::Components
{
    struct ProximityTrigger
    {
        static const u32 INVALID_NETWORK_ID = std::numeric_limits<u32>().max();

        u32 networkID = INVALID_NETWORK_ID;
        MetaGen::Shared::ProximityTrigger::ProximityTriggerFlagEnum flags = MetaGen::Shared::ProximityTrigger::ProximityTriggerFlagEnum::None;
        std::set<entt::entity> playersInside; // Entities currently inside the trigger
    };
}