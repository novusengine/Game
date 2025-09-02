#pragma once
#include <Base/Types.h>

#include <Meta/Generated/Shared/ProximityTriggerEnum.h>

#include <entt/fwd.hpp>

namespace ECS::Util
{
    namespace ProximityTriggerUtil
    {
        void CreateTrigger(entt::registry& registry, u32 triggerID, const std::string& name, Generated::ProximityTriggerFlagEnum flags, u16 mapID, const vec3& position, const vec3& extents);
        void DestroyTrigger(entt::registry& registry, u32 triggerID);
    }
}