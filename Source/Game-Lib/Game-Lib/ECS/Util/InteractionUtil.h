#pragma once

#include <Base/Types.h>

#include <entt/fwd.hpp>

namespace ECS::Util::Interaction
{
    bool Open(entt::registry& registry, entt::entity source);
    bool Select(entt::registry& registry, u64 sessionID, u32 revision, u64 optionToken);
    bool Close(entt::registry& registry, u64 sessionID);
}
