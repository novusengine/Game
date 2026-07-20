#pragma once

#include "Game-Lib/Gameplay/Faction/FactionRuntimeData.h"

#include <limits>

namespace ECS::Components
{
    struct UnitFaction
    {
    public:
        Gameplay::Faction::FactionID factionID = Gameplay::Faction::NONE_FACTION_ID;
        Gameplay::Faction::FactionIndex factionIndex = Gameplay::Faction::NONE_FACTION_INDEX;
        u8 playerReactionBounds = Gameplay::Faction::NEUTRAL_REACTION_BOUNDS;
        Gameplay::Faction::Reaction presentationReaction = Gameplay::Faction::Reaction::Neutral;
        u32 bucketSlot = std::numeric_limits<u32>().max();
    };
} // namespace ECS::Components
