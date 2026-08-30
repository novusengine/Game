#pragma once

#include <Base/Types.h>

#include <entt/fwd.hpp>

namespace ECS::Util::Interaction
{
    bool Open(entt::registry& registry, entt::entity source);
    bool Select(entt::registry& registry, u64 sessionID, u32 revision, u64 optionToken);
    bool Purchase(entt::registry& registry, u64 sessionID, u32 revision, u64 purchaseToken, u32 bundleCount);
    bool ToggleReward(entt::registry& registry, u32 selectionID);
    bool CompleteQuest(entt::registry& registry, u64 sessionID, u32 revision, u64 completionToken);
    bool AbandonQuest(entt::registry& registry, u32 questID);
    bool Close(entt::registry& registry, u64 sessionID);
}
