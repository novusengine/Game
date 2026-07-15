#pragma once

#include <Gameplay/Faction/FactionDefines.h>

#include <robinhood/robinhood.h>

#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace Gameplay::Faction
{
    using FactionIndex = u16;

    static constexpr FactionIndex NONE_FACTION_INDEX = 0;
    static constexpr FactionIndex INVALID_FACTION_INDEX = std::numeric_limits<FactionIndex>::max();

    struct ContentDefinition
    {
    public:
        FactionID id = NONE_FACTION_ID;
        std::string name = "None";
        u16 flags = 0;
        u16 defaultReactionToOthers = static_cast<u16>(Reaction::Neutral);
        u16 defaultPlayerReactionMin = static_cast<u16>(Reaction::Neutral);
        u16 defaultPlayerReactionMax = static_cast<u16>(Reaction::Neutral);
        i32 defaultReputationValue = 0;
    };

    struct ContentRelation
    {
    public:
        FactionID sourceFactionID = NONE_FACTION_ID;
        FactionID targetFactionID = NONE_FACTION_ID;
        u16 reaction = static_cast<u16>(Reaction::Neutral);
    };

    struct ContentStanding
    {
    public:
        StandingID id = 0;
        std::string name;
        i32 minimumValue = 0;
        u16 reaction = static_cast<u16>(Reaction::Neutral);
        u16 sortOrder = 0;
    };

    struct FactionContent
    {
    public:
        std::vector<ContentDefinition> definitions;
        std::vector<ContentRelation> relations;
        std::vector<ContentStanding> standings;
    };

    struct DefinitionRuntime
    {
    public:
        FactionID id = NONE_FACTION_ID;
        std::string name = "None";
        u16 flags = 0;
        Reaction defaultReactionToOthers = Reaction::Neutral;
        u8 defaultPlayerReactionBounds = NEUTRAL_REACTION_BOUNDS;
        i32 defaultReputationValue = 0;
    };

    struct StandingThreshold
    {
    public:
        StandingID id = 0;
        std::string name = "Neutral";
        i32 minimumValue = std::numeric_limits<i32>::min();
        Reaction reaction = Reaction::Neutral;
        u16 sortOrder = 0;
    };

    struct FactionRuntimeData
    {
    public:
        bool TryGetFactionIndex(FactionID id, FactionIndex& index) const;
        FactionID GetFactionID(FactionIndex index) const;
        const DefinitionRuntime& GetDefinition(FactionIndex index) const;
        Reaction GetRelation(FactionIndex source, FactionIndex target) const;
        const StandingThreshold& GetStanding(i32 value) const;

    public:
        robin_hood::unordered_flat_map<FactionID, FactionIndex> idToIndex;
        std::vector<FactionID> indexToID;
        std::vector<DefinitionRuntime> definitions;
        std::vector<u64> packedRelations;
        std::vector<StandingThreshold> standingThresholds;
        u32 wordsPerRelationRow = 0;
    };

    std::shared_ptr<const FactionRuntimeData> BuildRuntimeData(const FactionContent& content);
} // namespace Gameplay::Faction
