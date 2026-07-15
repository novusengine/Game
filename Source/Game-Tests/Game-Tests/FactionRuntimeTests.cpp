#include "Game-Lib/ECS/Components/UnitFaction.h"
#include "Game-Lib/ECS/Util/FactionUtil.h"
#include "Game-Lib/Gameplay/Faction/FactionRuntimeData.h"
#include "Game-Lib/Gameplay/Faction/FactionState.h"

#include <catch2/catch2.hpp>

#include <algorithm>
#include <utility>
#include <vector>

namespace
{
    using namespace Gameplay::Faction;

    FactionContent MakeContent()
    {
        FactionContent content;
        content.definitions.push_back({});
        content.definitions.push_back({
            .id = 10,
            .name = "Player",
            .defaultReactionToOthers = static_cast<u16>(Reaction::Hostile),
            .defaultPlayerReactionMin = static_cast<u16>(Reaction::Hostile),
            .defaultPlayerReactionMax = static_cast<u16>(Reaction::Friendly)
        });
        content.definitions.push_back({
            .id = 20,
            .name = "Reputation",
            .flags = static_cast<u16>(DefinitionFlags::AllowsReputation),
            .defaultReactionToOthers = static_cast<u16>(Reaction::Unfriendly),
            .defaultPlayerReactionMin = static_cast<u16>(Reaction::Hostile),
            .defaultPlayerReactionMax = static_cast<u16>(Reaction::Friendly)
        });
        content.definitions.push_back({
            .id = 30,
            .name = "Directional",
            .defaultReactionToOthers = static_cast<u16>(Reaction::Hostile),
            .defaultPlayerReactionMin = static_cast<u16>(Reaction::Hostile),
            .defaultPlayerReactionMax = static_cast<u16>(Reaction::Friendly)
        });
        content.relations.push_back({ .sourceFactionID = 10, .targetFactionID = 20, .reaction = static_cast<u16>(Reaction::Friendly) });
        content.relations.push_back({ .sourceFactionID = 20, .targetFactionID = 10, .reaction = static_cast<u16>(Reaction::Friendly) });
        content.relations.push_back({ .sourceFactionID = 10, .targetFactionID = 30, .reaction = static_cast<u16>(Reaction::Friendly) });
        content.standings.push_back({
            .id = 1,
            .name = "Hostile",
            .minimumValue = -1000,
            .reaction = static_cast<u16>(Reaction::Hostile),
            .sortOrder = 1
        });
        content.standings.push_back({
            .id = 2,
            .name = "Unfriendly",
            .minimumValue = -500,
            .reaction = static_cast<u16>(Reaction::Unfriendly),
            .sortOrder = 2
        });
        content.standings.push_back({
            .id = 3,
            .name = "Neutral",
            .minimumValue = 0,
            .reaction = static_cast<u16>(Reaction::Neutral),
            .sortOrder = 3
        });
        content.standings.push_back({
            .id = 4,
            .name = "Friendly",
            .minimumValue = 1000,
            .reaction = static_cast<u16>(Reaction::Friendly),
            .sortOrder = 4
        });

        return content;
    }

    std::vector<entt::entity> reactionChangedEntities;

    void RecordReactionChange(entt::entity entity, Reaction oldReaction, Reaction newReaction)
    {
        (void)oldReaction;
        (void)newReaction;
        reactionChangedEntities.push_back(entity);
    }
} // namespace

TEST_CASE("Client faction matrix matches directional server vectors", "[Faction][Client]")
{
    const auto runtime = BuildRuntimeData(MakeContent());
    FactionIndex none = INVALID_FACTION_INDEX;
    FactionIndex player = INVALID_FACTION_INDEX;
    FactionIndex reputation = INVALID_FACTION_INDEX;
    REQUIRE(runtime->TryGetFactionIndex(0, none));
    REQUIRE(runtime->TryGetFactionIndex(10, player));
    REQUIRE(runtime->TryGetFactionIndex(20, reputation));

    CHECK(none == NONE_FACTION_INDEX);
    CHECK(runtime->GetFactionID(player) == 10);
    CHECK(runtime->GetRelation(none, player) == Reaction::Neutral);
    CHECK(runtime->GetRelation(player, none) == Reaction::Neutral);
    CHECK(runtime->GetRelation(player, player) == Reaction::Friendly);
    CHECK(runtime->GetRelation(player, reputation) == Reaction::Friendly);
    CHECK(runtime->GetRelation(reputation, player) == Reaction::Friendly);
    CHECK(runtime->GetStanding(-1000).reaction == Reaction::Hostile);
    CHECK(runtime->GetStanding(-500).reaction == Reaction::Unfriendly);
    CHECK(runtime->GetStanding(0).reaction == Reaction::Neutral);
    CHECK(runtime->GetStanding(1000).reaction == Reaction::Friendly);
}

TEST_CASE("Client faction state does not synthesize runtime data", "[Faction][Client]")
{
    entt::registry registry;
    ECS::Util::Faction::SetEventCallbacks(registry, RecordReactionChange, nullptr);

    const auto& state = registry.ctx().get<Gameplay::Faction::FactionState>();
    CHECK_FALSE(state.runtime);
    CHECK(ECS::Util::Faction::GetPersistentStanding(registry, 20) == nullptr);
    CHECK(ECS::Util::Faction::GetEffectiveStanding(registry, 20) == nullptr);

    const entt::entity entity = registry.create();
    CHECK_FALSE(ECS::Util::Faction::AttachUnit(registry, entity));
}

TEST_CASE("Client faction matrix packing crosses words and rows", "[Faction][Client]")
{
    FactionContent content;
    content.definitions.push_back({});
    for (u16 id = 1; id <= 34; ++id)
    {
        content.definitions.push_back({
            .id = id,
            .name = "Faction",
            .defaultReactionToOthers = static_cast<u16>(Reaction::Neutral),
            .defaultPlayerReactionMin = static_cast<u16>(Reaction::Hostile),
            .defaultPlayerReactionMax = static_cast<u16>(Reaction::Friendly)
        });
    }
    content.relations.push_back({ .sourceFactionID = 1, .targetFactionID = 32, .reaction = static_cast<u16>(Reaction::Hostile) });
    content.relations.push_back({ .sourceFactionID = 1, .targetFactionID = 33, .reaction = static_cast<u16>(Reaction::Friendly) });
    content.relations.push_back({ .sourceFactionID = 34, .targetFactionID = 2, .reaction = static_cast<u16>(Reaction::Unfriendly) });

    const auto runtime = BuildRuntimeData(content);
    FactionIndex one = INVALID_FACTION_INDEX;
    FactionIndex two = INVALID_FACTION_INDEX;
    FactionIndex thirtyTwo = INVALID_FACTION_INDEX;
    FactionIndex thirtyThree = INVALID_FACTION_INDEX;
    FactionIndex thirtyFour = INVALID_FACTION_INDEX;
    REQUIRE(runtime->TryGetFactionIndex(1, one));
    REQUIRE(runtime->TryGetFactionIndex(2, two));
    REQUIRE(runtime->TryGetFactionIndex(32, thirtyTwo));
    REQUIRE(runtime->TryGetFactionIndex(33, thirtyThree));
    REQUIRE(runtime->TryGetFactionIndex(34, thirtyFour));
    CHECK(runtime->GetRelation(one, thirtyTwo) == Reaction::Hostile);
    CHECK(runtime->GetRelation(one, thirtyThree) == Reaction::Friendly);
    CHECK(runtime->GetRelation(thirtyFour, two) == Reaction::Unfriendly);
}

TEST_CASE("Client faction buckets swap-remove and recalculate only affected units", "[Faction][Client]")
{
    entt::registry registry;
    ECS::Util::Faction::Initialize(registry, BuildRuntimeData(MakeContent()));
    ECS::Util::Faction::SetEventCallbacks(registry, RecordReactionChange, nullptr);

    const entt::entity local = registry.create();
    const entt::entity reputationA = registry.create();
    const entt::entity reputationB = registry.create();
    const entt::entity directional = registry.create();
    REQUIRE(ECS::Util::Faction::AttachUnit(registry, local));
    REQUIRE(ECS::Util::Faction::AttachUnit(registry, reputationA));
    REQUIRE(ECS::Util::Faction::AttachUnit(registry, reputationB));
    REQUIRE(ECS::Util::Faction::AttachUnit(registry, directional));

    const u8 fullBounds = ReactionBounds{ Reaction::Hostile, Reaction::Friendly }.Pack();
    REQUIRE(ECS::Util::Faction::ApplyUnitUpdate(registry, local, 10, fullBounds));
    ECS::Util::Faction::SetLocalPlayer(registry, local);
    REQUIRE(ECS::Util::Faction::ApplyUnitUpdate(registry, reputationA, 20, fullBounds));
    REQUIRE(ECS::Util::Faction::ApplyUnitUpdate(registry, reputationB, 20, fullBounds));
    REQUIRE(ECS::Util::Faction::ApplyUnitUpdate(registry, directional, 30, fullBounds));

    auto& state = registry.ctx().get<Gameplay::Faction::FactionState>();
    FactionIndex reputationIndex = INVALID_FACTION_INDEX;
    FactionIndex directionalIndex = INVALID_FACTION_INDEX;
    REQUIRE(state.runtime->TryGetFactionIndex(20, reputationIndex));
    REQUIRE(state.runtime->TryGetFactionIndex(30, directionalIndex));
    REQUIRE(state.unitsByFaction[reputationIndex].size() == 2);

    reactionChangedEntities.clear();
    REQUIRE(ECS::Util::Faction::ApplyReputationUpdate(registry, 20, -1000, 0, true));
    CHECK(reactionChangedEntities.size() == 2);
    CHECK(std::ranges::find(reactionChangedEntities, directional) == reactionChangedEntities.end());
    CHECK(ECS::Util::Faction::RecalculateFactionBucket(registry, reputationIndex) == 2);

    REQUIRE(ECS::Util::Faction::DetachUnit(registry, reputationA));
    REQUIRE(state.unitsByFaction[reputationIndex].size() == 1);
    CHECK(state.unitsByFaction[reputationIndex][0] == reputationB);
    CHECK(registry.get<ECS::Components::UnitFaction>(reputationB).bucketSlot == 0);

    REQUIRE(ECS::Util::Faction::ApplyUnitUpdate(registry, reputationB, 30, fullBounds));
    CHECK(state.unitsByFaction[reputationIndex].empty());
    CHECK(state.unitsByFaction[directionalIndex].size() == 2);

    reactionChangedEntities.clear();
    const u8 neutralBounds = ReactionBounds{ Reaction::Neutral, Reaction::Friendly }.Pack();
    REQUIRE(ECS::Util::Faction::ApplyUnitUpdate(registry, directional, 30, neutralBounds));
    CHECK(reactionChangedEntities.size() <= 1);
}

TEST_CASE("Client faction packet state is absolute idempotent and supports deletion", "[Faction][Client]")
{
    entt::registry registry;
    ECS::Util::Faction::Initialize(registry, BuildRuntimeData(MakeContent()));

    const entt::entity unit = registry.create();
    REQUIRE(ECS::Util::Faction::AttachUnit(registry, unit));
    const u8 fullBounds = ReactionBounds{ Reaction::Hostile, Reaction::Friendly }.Pack();
    REQUIRE(ECS::Util::Faction::ApplyUnitUpdate(registry, unit, 20, fullBounds));
    CHECK_FALSE(ECS::Util::Faction::ApplyUnitUpdate(registry, unit, 20, fullBounds));

    REQUIRE(ECS::Util::Faction::ApplyReputationUpdate(registry, 20, 125, 1, true));
    CHECK_FALSE(ECS::Util::Faction::ApplyReputationUpdate(registry, 20, 125, 1, true));
    Gameplay::Faction::ReputationState reputation;
    REQUIRE(ECS::Util::Faction::FindReputation(registry, 20, reputation));
    CHECK(reputation.value == 125);
    REQUIRE(ECS::Util::Faction::ApplyReputationUpdate(registry, 20, 0, 0, false));
    CHECK_FALSE(ECS::Util::Faction::FindReputation(registry, 20, reputation));
    CHECK_FALSE(ECS::Util::Faction::ApplyReputationUpdate(registry, 20, 999, 7, false));

    REQUIRE(ECS::Util::Faction::ApplyPerceptionUpdate(registry, 20, static_cast<u8>(Gameplay::Faction::PerceptionOverrideFields::Standing), 1000, 0));
    CHECK_FALSE(ECS::Util::Faction::ApplyPerceptionUpdate(registry, 20, static_cast<u8>(Gameplay::Faction::PerceptionOverrideFields::Standing), 1000, 0));
    REQUIRE(ECS::Util::Faction::ApplyPerceptionUpdate(registry, 20, static_cast<u8>(Gameplay::Faction::PerceptionOverrideFields::Reaction), 0, static_cast<u8>(Reaction::Hostile)));
    CHECK(ECS::Util::Faction::GetPresentationReaction(registry, unit) == Reaction::Hostile);
    REQUIRE(ECS::Util::Faction::ApplyPerceptionUpdate(registry, 20, 0, 0, 0));
    CHECK_FALSE(ECS::Util::Faction::ApplyPerceptionUpdate(registry, 20, 0, 0, 0));

    REQUIRE(ECS::Util::Faction::ApplyUnitUpdate(registry, unit, 65000, fullBounds));
    const auto& unitFaction = registry.get<ECS::Components::UnitFaction>(unit);
    CHECK(unitFaction.factionID == NONE_FACTION_ID);
    CHECK(unitFaction.factionIndex == NONE_FACTION_INDEX);
    CHECK(unitFaction.presentationReaction == Reaction::Neutral);
}

TEST_CASE("Client presentation uses the unit reaction to the local player", "[Faction][Client]")
{
    FactionContent content;
    content.definitions.push_back({});
    content.definitions.push_back({
        .id = 10,
        .name = "Player",
        .defaultPlayerReactionMin = static_cast<u16>(Reaction::Hostile),
        .defaultPlayerReactionMax = static_cast<u16>(Reaction::Friendly)
    });

    u16 nextFactionID = 20;
    for (u8 forward = 0; forward <= static_cast<u8>(Reaction::Friendly); ++forward)
    {
        for (u8 reverse = 0; reverse <= static_cast<u8>(Reaction::Friendly); ++reverse)
        {
            const u16 factionID = nextFactionID++;
            content.definitions.push_back({
                .id = factionID,
                .name = "Combination",
                .defaultPlayerReactionMin = static_cast<u16>(Reaction::Hostile),
                .defaultPlayerReactionMax = static_cast<u16>(Reaction::Friendly)
            });
            content.relations.push_back({ .sourceFactionID = 10, .targetFactionID = factionID, .reaction = forward });
            content.relations.push_back({ .sourceFactionID = factionID, .targetFactionID = 10, .reaction = reverse });
        }
    }

    entt::registry registry;
    ECS::Util::Faction::Initialize(registry, BuildRuntimeData(content));
    const u8 fullBounds = ReactionBounds{ Reaction::Hostile, Reaction::Friendly }.Pack();
    const entt::entity local = registry.create();
    REQUIRE(ECS::Util::Faction::AttachUnit(registry, local));
    REQUIRE(ECS::Util::Faction::ApplyUnitUpdate(registry, local, 10, fullBounds));
    ECS::Util::Faction::SetLocalPlayer(registry, local);

    nextFactionID = 20;
    for (u8 forward = 0; forward <= static_cast<u8>(Reaction::Friendly); ++forward)
    {
        for (u8 reverse = 0; reverse <= static_cast<u8>(Reaction::Friendly); ++reverse)
        {
            const entt::entity unit = registry.create();
            REQUIRE(ECS::Util::Faction::AttachUnit(registry, unit));
            REQUIRE(ECS::Util::Faction::ApplyUnitUpdate(registry, unit, nextFactionID++, fullBounds));
            CHECK(ECS::Util::Faction::GetLocalReactionToUnit(registry, unit) == static_cast<Reaction>(forward));
            CHECK(ECS::Util::Faction::GetUnitReactionToLocalPlayer(registry, unit) == static_cast<Reaction>(reverse));

            const Reaction presentation = ECS::Util::Faction::GetPresentationReaction(registry, unit);
            CHECK(presentation == static_cast<Reaction>(reverse));
            CHECK(ECS::Util::Faction::CanAttack(registry, unit) == (forward != static_cast<u8>(Reaction::Friendly) && reverse != static_cast<u8>(Reaction::Friendly)));

            if (forward == static_cast<u8>(Reaction::Neutral) && reverse == static_cast<u8>(Reaction::Friendly))
            {
                CHECK(presentation == Reaction::Friendly);
            }
            if (forward == static_cast<u8>(Reaction::Friendly) && reverse == static_cast<u8>(Reaction::Neutral))
            {
                CHECK(presentation == Reaction::Neutral);
            }
        }
    }
}

TEST_CASE("Client perception overrides take precedence over normal presentation", "[Faction][Client]")
{
    FactionContent content;
    content.definitions.push_back({});
    content.definitions.push_back({
        .id = 10,
        .name = "Player",
        .defaultPlayerReactionMin = static_cast<u16>(Reaction::Hostile),
        .defaultPlayerReactionMax = static_cast<u16>(Reaction::Friendly)
    });
    content.definitions.push_back({
        .id = 20,
        .name = "Unit",
        .defaultPlayerReactionMin = static_cast<u16>(Reaction::Hostile),
        .defaultPlayerReactionMax = static_cast<u16>(Reaction::Friendly)
    });
    content.relations.push_back({ .sourceFactionID = 10, .targetFactionID = 20, .reaction = static_cast<u16>(Reaction::Friendly) });
    content.relations.push_back({ .sourceFactionID = 20, .targetFactionID = 10, .reaction = static_cast<u16>(Reaction::Neutral) });
    content.standings.push_back({
        .id = 1,
        .name = "Hostile",
        .minimumValue = -1000,
        .reaction = static_cast<u16>(Reaction::Hostile),
        .sortOrder = 1
    });
    content.standings.push_back({
        .id = 2,
        .name = "Neutral",
        .minimumValue = 0,
        .reaction = static_cast<u16>(Reaction::Neutral),
        .sortOrder = 2
    });
    content.standings.push_back({
        .id = 3,
        .name = "Friendly",
        .minimumValue = 1000,
        .reaction = static_cast<u16>(Reaction::Friendly),
        .sortOrder = 3
    });

    entt::registry registry;
    ECS::Util::Faction::Initialize(registry, BuildRuntimeData(content));
    ECS::Util::Faction::SetEventCallbacks(registry, RecordReactionChange, nullptr);
    const u8 fullBounds = ReactionBounds{ Reaction::Hostile, Reaction::Friendly }.Pack();
    const entt::entity local = registry.create();
    const entt::entity unit = registry.create();
    REQUIRE(ECS::Util::Faction::AttachUnit(registry, local));
    REQUIRE(ECS::Util::Faction::AttachUnit(registry, unit));
    REQUIRE(ECS::Util::Faction::ApplyUnitUpdate(registry, local, 10, fullBounds));
    ECS::Util::Faction::SetLocalPlayer(registry, local);
    REQUIRE(ECS::Util::Faction::ApplyUnitUpdate(registry, unit, 20, fullBounds));

    CHECK(ECS::Util::Faction::GetLocalReactionToUnit(registry, unit) == Reaction::Friendly);
    CHECK(ECS::Util::Faction::GetUnitReactionToLocalPlayer(registry, unit) == Reaction::Neutral);
    CHECK(ECS::Util::Faction::GetPresentationReaction(registry, unit) == Reaction::Neutral);
    CHECK_FALSE(ECS::Util::Faction::CanAttack(registry, unit));

    reactionChangedEntities.clear();
    const u8 standingField = static_cast<u8>(PerceptionOverrideFields::Standing);
    const u8 reactionField = static_cast<u8>(PerceptionOverrideFields::Reaction);
    REQUIRE(ECS::Util::Faction::ApplyPerceptionUpdate(registry, 20, standingField, 1000, 0));
    CHECK(ECS::Util::Faction::GetPresentationReaction(registry, unit) == Reaction::Friendly);
    CHECK_FALSE(ECS::Util::Faction::CanAttack(registry, unit));
    REQUIRE(reactionChangedEntities.size() == 1);

    REQUIRE(ECS::Util::Faction::ApplyPerceptionUpdate(registry, 20, standingField | reactionField, -1000, static_cast<u8>(Reaction::Friendly)));
    CHECK(ECS::Util::Faction::GetPresentationReaction(registry, unit) == Reaction::Friendly);
    CHECK_FALSE(ECS::Util::Faction::CanAttack(registry, unit));
    CHECK(reactionChangedEntities.size() == 1);
    CHECK(ECS::Util::Faction::GetUnitReactionToLocalPlayer(registry, unit) == Reaction::Neutral);

    REQUIRE(ECS::Util::Faction::ApplyPerceptionUpdate(registry, 20, reactionField, 0, static_cast<u8>(Reaction::Hostile)));
    CHECK(ECS::Util::Faction::GetPresentationReaction(registry, unit) == Reaction::Hostile);
    CHECK(ECS::Util::Faction::CanAttack(registry, unit));
    REQUIRE(reactionChangedEntities.size() == 2);

    REQUIRE(ECS::Util::Faction::ApplyPerceptionUpdate(registry, 20, 0, 0, 0));
    CHECK(ECS::Util::Faction::GetPresentationReaction(registry, unit) == Reaction::Neutral);
    CHECK_FALSE(ECS::Util::Faction::CanAttack(registry, unit));
    CHECK(reactionChangedEntities.size() == 3);

    const u8 cappedBounds = ReactionBounds{ Reaction::Hostile, Reaction::Neutral }.Pack();
    REQUIRE(ECS::Util::Faction::ApplyUnitUpdate(registry, unit, 20, cappedBounds));
    reactionChangedEntities.clear();
    REQUIRE(ECS::Util::Faction::ApplyPerceptionUpdate(registry, 20, standingField, 1000, 0));
    CHECK(ECS::Util::Faction::GetPresentationReaction(registry, unit) == Reaction::Neutral);
    CHECK(reactionChangedEntities.empty());

    REQUIRE(ECS::Util::Faction::ApplyPerceptionUpdate(registry, 20, reactionField, 0, static_cast<u8>(Reaction::Friendly)));
    CHECK(ECS::Util::Faction::GetPresentationReaction(registry, unit) == Reaction::Friendly);
    CHECK(reactionChangedEntities.size() == 1);
}

TEST_CASE("Client At War reputation permits attacking an otherwise friendly faction", "[Faction][Client]")
{
    FactionContent content;
    content.definitions.push_back({});
    content.definitions.push_back({
        .id = 10,
        .name = "Player",
        .defaultPlayerReactionMin = static_cast<u16>(Reaction::Hostile),
        .defaultPlayerReactionMax = static_cast<u16>(Reaction::Friendly)
    });
    content.definitions.push_back({
        .id = 20,
        .name = "Reputation",
        .flags = static_cast<u16>(DefinitionFlags::AllowsReputation) | static_cast<u16>(DefinitionFlags::CanSetAtWar),
        .defaultPlayerReactionMin = static_cast<u16>(Reaction::Hostile),
        .defaultPlayerReactionMax = static_cast<u16>(Reaction::Friendly)
    });
    content.relations.push_back({ .sourceFactionID = 10, .targetFactionID = 20, .reaction = static_cast<u16>(Reaction::Friendly) });
    content.relations.push_back({ .sourceFactionID = 20, .targetFactionID = 10, .reaction = static_cast<u16>(Reaction::Friendly) });
    content.standings.push_back({
        .id = 1,
        .name = "Neutral",
        .minimumValue = 0,
        .reaction = static_cast<u16>(Reaction::Neutral),
        .sortOrder = 1
    });
    content.standings.push_back({
        .id = 2,
        .name = "Friendly",
        .minimumValue = 1000,
        .reaction = static_cast<u16>(Reaction::Friendly),
        .sortOrder = 2
    });

    entt::registry registry;
    ECS::Util::Faction::Initialize(registry, BuildRuntimeData(content));
    const u8 fullBounds = ReactionBounds{ Reaction::Hostile, Reaction::Friendly }.Pack();
    const entt::entity local = registry.create();
    const entt::entity unit = registry.create();
    REQUIRE(ECS::Util::Faction::AttachUnit(registry, local));
    REQUIRE(ECS::Util::Faction::AttachUnit(registry, unit));
    REQUIRE(ECS::Util::Faction::ApplyUnitUpdate(registry, local, 10, fullBounds));
    ECS::Util::Faction::SetLocalPlayer(registry, local);
    REQUIRE(ECS::Util::Faction::ApplyUnitUpdate(registry, unit, 20, fullBounds));

    CHECK_FALSE(ECS::Util::Faction::CanAttack(registry, unit));
    REQUIRE(ECS::Util::Faction::ApplyReputationUpdate(registry, 20, 1000, static_cast<u16>(ReputationFlags::AtWar), true));
    CHECK(ECS::Util::Faction::CanAttack(registry, unit));

    REQUIRE(ECS::Util::Faction::ApplyReputationUpdate(registry, 20, 1000, 0, true));
    CHECK_FALSE(ECS::Util::Faction::CanAttack(registry, unit));
}
