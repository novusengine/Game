#pragma once

#include "Game-Lib/Gameplay/Faction/FactionRuntimeData.h"

#include <entt/entt.hpp>
#include <robinhood/robinhood.h>

#include <memory>
#include <vector>

namespace Gameplay::Faction
{
    struct ReputationState
    {
    public:
        bool operator==(const ReputationState&) const = default;

    public:
        i32 value = 0;
        u16 flags = 0;
    };

    enum class PerceptionOverrideFields : u8
    {
        None = 0,
        Standing = 1 << 0,
        Reaction = 1 << 1
    };

    static constexpr u8 PERCEPTION_OVERRIDE_FIELD_MASK = static_cast<u8>(PerceptionOverrideFields::Standing) | static_cast<u8>(PerceptionOverrideFields::Reaction);

    struct PerceptionOverride
    {
    public:
        bool operator==(const PerceptionOverride&) const = default;

    public:
        u8 activeFields = 0;
        i32 effectiveStandingValue = 0;
        Reaction effectiveReaction = Reaction::Neutral;
    };

    struct ReputationChange
    {
    public:
        FactionID factionID = NONE_FACTION_ID;
        i32 oldValue = 0;
        i32 newValue = 0;
        u16 oldFlags = 0;
        u16 newFlags = 0;
        StandingID oldPersistentStandingID = 0;
        StandingID newPersistentStandingID = 0;
        StandingID oldEffectiveStandingID = 0;
        StandingID newEffectiveStandingID = 0;
        u8 oldPerceptionFields = 0;
        u8 newPerceptionFields = 0;
        bool wasPresent = false;
        bool isPresent = false;
    };

    using UnitReactionChangedCallback = void (*)(entt::entity, Reaction, Reaction);
    using ReputationChangedCallback = void (*)(const ReputationChange&);

    struct FactionState
    {
    public:
        std::shared_ptr<const FactionRuntimeData> runtime;
        robin_hood::unordered_flat_map<FactionIndex, ReputationState> reputationByFaction;
        robin_hood::unordered_flat_map<FactionIndex, PerceptionOverride> perceptionByFaction;
        robin_hood::unordered_flat_set<FactionID> reportedUnknownFactionIDs;
        std::vector<std::vector<entt::entity>> unitsByFaction;
        entt::entity localPlayer = entt::null;
        FactionIndex localPlayerFaction = NONE_FACTION_INDEX;
        UnitReactionChangedCallback unitReactionChanged = nullptr;
        ReputationChangedCallback reputationChanged = nullptr;
    };
} // namespace Gameplay::Faction
