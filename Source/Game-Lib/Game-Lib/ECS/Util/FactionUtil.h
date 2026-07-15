#pragma once

#include "Game-Lib/Gameplay/Faction/FactionState.h"

#include <entt/entt.hpp>

#include <memory>

namespace ECS::Util::Faction
{
    void Initialize(entt::registry& registry, std::shared_ptr<const Gameplay::Faction::FactionRuntimeData> runtime);
    void SetEventCallbacks(entt::registry& registry, Gameplay::Faction::UnitReactionChangedCallback unitReactionChanged, Gameplay::Faction::ReputationChangedCallback reputationChanged);

    bool AttachUnit(entt::registry& registry, entt::entity entity);
    bool DetachUnit(entt::registry& registry, entt::entity entity);
    bool ApplyUnitUpdate(entt::registry& registry, entt::entity entity, Gameplay::Faction::FactionID factionID, u8 playerReactionBounds);
    bool ApplyReputationUpdate(entt::registry& registry, Gameplay::Faction::FactionID factionID, i32 value, u16 flags, bool isPresent);
    bool ApplyPerceptionUpdate(entt::registry& registry, Gameplay::Faction::FactionID factionID, u8 activeFields, i32 effectiveStandingValue, u8 effectiveReaction);

    void SetLocalPlayer(entt::registry& registry, entt::entity entity);
    void ResetOwnerState(entt::registry& registry);

    Gameplay::Faction::Reaction GetLocalReactionToUnit(const entt::registry& registry, entt::entity entity);
    Gameplay::Faction::Reaction GetUnitReactionToLocalPlayer(const entt::registry& registry, entt::entity entity);
    Gameplay::Faction::Reaction GetPresentationReaction(const entt::registry& registry, entt::entity entity);
    bool CanAttack(const entt::registry& registry, entt::entity entity);
    const Gameplay::Faction::StandingThreshold* GetPersistentStanding(const entt::registry& registry, Gameplay::Faction::FactionID factionID);
    const Gameplay::Faction::StandingThreshold* GetEffectiveStanding(const entt::registry& registry, Gameplay::Faction::FactionID factionID);
    i32 GetPersistentReputationValue(const entt::registry& registry, Gameplay::Faction::FactionID factionID);
    bool FindReputation(const entt::registry& registry, Gameplay::Faction::FactionID factionID, Gameplay::Faction::ReputationState& reputation);

    bool RecalculateEntity(entt::registry& registry, entt::entity entity);
    u32 RecalculateFactionBucket(entt::registry& registry, Gameplay::Faction::FactionIndex factionIndex);
    u32 RecalculateAll(entt::registry& registry);
} // namespace ECS::Util::Faction
