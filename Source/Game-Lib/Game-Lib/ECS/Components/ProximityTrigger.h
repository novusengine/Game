#pragma once
#include <Base/Types.h>

#include <Meta/Generated/Shared/ProximityTriggerEnum.h>

#include <entt/entt.hpp>
#include <set>

namespace ECS::Components
{
    struct ProximityTrigger
    {
        static const u32 INVALID_NETWORK_ID = std::numeric_limits<u32>().max();

        u32 networkID = INVALID_NETWORK_ID;
        Generated::ProximityTriggerFlagEnum flags = Generated::ProximityTriggerFlagEnum::None;
        std::set<entt::entity> playersInside; // Entities currently inside the trigger
    };
}