#include "FactionRuntimeData.h"

#include <Base/Util/DebugHandler.h>

#include <algorithm>
#include <iterator>

namespace Gameplay::Faction
{
    namespace
    {
        u32 MakePairKey(FactionIndex source, FactionIndex target)
        {
            return (static_cast<u32>(source) << 16u) | static_cast<u32>(target);
        }

        void SetRelation(FactionRuntimeData& runtime, FactionIndex source, FactionIndex target, Reaction reaction)
        {
            const size_t wordIndex = static_cast<size_t>(source) * runtime.wordsPerRelationRow + target / 32u;
            const u32 shift = static_cast<u32>(target % 32u) * 2u;
            const u64 mask = u64{ 0x3 } << shift;
            runtime.packedRelations[wordIndex] = (runtime.packedRelations[wordIndex] & ~mask) | (static_cast<u64>(reaction) << shift);
        }

        bool TryPackBounds(u16 minimum, u16 maximum, u8& packed)
        {
            if (!IsValidReaction(minimum) || !IsValidReaction(maximum) || minimum > maximum)
                return false;

            packed = ReactionBounds{ .minimum = static_cast<Reaction>(minimum), .maximum = static_cast<Reaction>(maximum) }.Pack();
            return true;
        }

        void ResetStandingToNeutral(FactionRuntimeData& runtime)
        {
            runtime.standingThresholds.clear();
            runtime.standingThresholds.emplace_back();
        }
    } // namespace

    bool FactionRuntimeData::TryGetFactionIndex(FactionID id, FactionIndex& index) const
    {
        const auto itr = idToIndex.find(id);
        if (itr == idToIndex.end())
        {
            index = INVALID_FACTION_INDEX;
            return false;
        }

        index = itr->second;
        return true;
    }

    FactionID FactionRuntimeData::GetFactionID(FactionIndex index) const
    {
        return index < indexToID.size() ? indexToID[index] : NONE_FACTION_ID;
    }

    const DefinitionRuntime& FactionRuntimeData::GetDefinition(FactionIndex index) const
    {
        NC_ASSERT(!definitions.empty(), "Faction runtime has no definitions");
        return definitions.at(index < definitions.size() ? index : NONE_FACTION_INDEX);
    }

    Reaction FactionRuntimeData::GetRelation(FactionIndex source, FactionIndex target) const
    {
        if (source >= definitions.size() || target >= definitions.size() || wordsPerRelationRow == 0)
            return Reaction::Neutral;

        const size_t wordIndex = static_cast<size_t>(source) * wordsPerRelationRow + target / 32u;
        const u32 shift = static_cast<u32>(target % 32u) * 2u;
        return static_cast<Reaction>((packedRelations[wordIndex] >> shift) & 0x3u);
    }

    const StandingThreshold& FactionRuntimeData::GetStanding(i32 value) const
    {
        NC_ASSERT(!standingThresholds.empty(), "Faction runtime has no standing thresholds");

        const auto itr = std::upper_bound(standingThresholds.begin(), standingThresholds.end(), value, [](i32 candidate, const StandingThreshold& threshold)
        {
            return candidate < threshold.minimumValue;
        });

        return itr == standingThresholds.begin() ? standingThresholds.at(0) : *std::prev(itr);
    }

    std::shared_ptr<const FactionRuntimeData> BuildRuntimeData(const FactionContent& content)
    {
        FactionRuntimeData runtime;
        runtime.idToIndex.reserve(content.definitions.size() + 1u);
        runtime.indexToID.reserve(content.definitions.size() + 1u);
        runtime.definitions.reserve(content.definitions.size() + 1u);

        const ContentDefinition* noneRecord = nullptr;
        std::vector<const ContentDefinition*> sortedDefinitions;
        robin_hood::unordered_flat_set<FactionID> factionIDs;
        sortedDefinitions.reserve(content.definitions.size());
        factionIDs.reserve(content.definitions.size() + 1u);

        for (const ContentDefinition& definition : content.definitions)
        {
            if (!factionIDs.insert(definition.id).second)
            {
                NC_LOG_ERROR("Skipped duplicate client faction definition with ID {0}", definition.id);
                continue;
            }

            if (definition.id == NONE_FACTION_ID)
            {
                noneRecord = &definition;
            }
            else
            {
                sortedDefinitions.push_back(&definition);
            }
        }

        std::sort(sortedDefinitions.begin(), sortedDefinitions.end(), [](const auto* left, const auto* right)
        {
            return left->id < right->id;
        });

        const size_t maximumDefinitions = static_cast<size_t>(INVALID_FACTION_INDEX) - 1u;
        if (sortedDefinitions.size() > maximumDefinitions)
        {
            const size_t definitionCount = sortedDefinitions.size();
            sortedDefinitions.resize(maximumDefinitions);

            NC_LOG_ERROR("Client faction content contains {0} nonzero definitions, but only {1} fit in the runtime index range", definitionCount, maximumDefinitions);
        }

        DefinitionRuntime noneDefinition;
        if (noneRecord)
        {
            noneDefinition.name = noneRecord->name;
            if (noneRecord->flags != 0 || noneRecord->defaultReactionToOthers != static_cast<u16>(Reaction::Neutral) || noneRecord->defaultPlayerReactionMin != static_cast<u16>(Reaction::Neutral) || noneRecord->defaultPlayerReactionMax != static_cast<u16>(Reaction::Neutral) || noneRecord->defaultReputationValue != 0)
            {
                NC_LOG_ERROR("Client faction 0 is not a strict neutral None definition; neutral runtime values will be used");
            }
        }
        else
        {
            NC_LOG_WARNING("Client faction 0 is missing; a strict neutral None definition was synthesized");
        }

        runtime.idToIndex.emplace(NONE_FACTION_ID, NONE_FACTION_INDEX);
        runtime.indexToID.push_back(NONE_FACTION_ID);
        runtime.definitions.push_back(std::move(noneDefinition));

        bool anyFactionAllowsReputation = false;
        for (const ContentDefinition* definition : sortedDefinitions)
        {
            u16 flags = definition->flags;
            const u16 unknownFlags = flags & ~DEFINITION_FLAG_MASK;
            if (unknownFlags != 0)
            {
                flags &= DEFINITION_FLAG_MASK;

                NC_LOG_ERROR("Client faction {0} has unknown definition flags {1}; unknown flags are ignored", definition->id, unknownFlags);
            }

            Reaction defaultReaction = Reaction::Neutral;
            if (IsValidReaction(definition->defaultReactionToOthers))
            {
                defaultReaction = static_cast<Reaction>(definition->defaultReactionToOthers);
            }
            else
            {
                NC_LOG_ERROR("Client faction {0} has invalid default reaction {1}; Neutral will be used", definition->id, definition->defaultReactionToOthers);
            }

            u8 bounds = NEUTRAL_REACTION_BOUNDS;
            if (!TryPackBounds(definition->defaultPlayerReactionMin, definition->defaultPlayerReactionMax, bounds))
            {
                NC_LOG_ERROR("Client faction {0} has invalid player reaction bounds {1}..{2}; Neutral..Neutral will be used", definition->id, definition->defaultPlayerReactionMin, definition->defaultPlayerReactionMax);
            }

            const bool allowsReputation = HasFlag(flags, DefinitionFlags::AllowsReputation);
            i32 defaultReputationValue = definition->defaultReputationValue;
            if (!allowsReputation && defaultReputationValue != 0)
            {
                defaultReputationValue = 0;

                NC_LOG_ERROR("Client faction {0} has default reputation {1} but does not allow reputation; zero will be used", definition->id, defaultReputationValue);
            }

            const auto index = static_cast<FactionIndex>(runtime.definitions.size());
            runtime.idToIndex.emplace(definition->id, index);
            runtime.indexToID.push_back(definition->id);
            runtime.definitions.push_back({
                .id = definition->id,
                .name = definition->name,
                .flags = flags,
                .defaultReactionToOthers = defaultReaction,
                .defaultPlayerReactionBounds = bounds,
                .defaultReputationValue = defaultReputationValue
            });

            anyFactionAllowsReputation |= allowsReputation;
        }

        const size_t factionCount = runtime.definitions.size();
        runtime.wordsPerRelationRow = static_cast<u32>((factionCount + 31u) / 32u);
        runtime.packedRelations.assign(factionCount * runtime.wordsPerRelationRow, 0);

        for (size_t source = 0; source < factionCount; ++source)
        {
            const auto sourceIndex = static_cast<FactionIndex>(source);
            for (size_t target = 0; target < factionCount; ++target)
            {
                SetRelation(runtime, sourceIndex, static_cast<FactionIndex>(target), runtime.definitions[source].defaultReactionToOthers);
            }

            SetRelation(runtime, sourceIndex, NONE_FACTION_INDEX, Reaction::Neutral);
            SetRelation(runtime, sourceIndex, sourceIndex, sourceIndex == NONE_FACTION_INDEX ? Reaction::Neutral : Reaction::Friendly);
        }

        for (size_t target = 0; target < factionCount; ++target)
        {
            SetRelation(runtime, NONE_FACTION_INDEX, static_cast<FactionIndex>(target), Reaction::Neutral);
        }

        robin_hood::unordered_flat_set<u32> relationPairs;
        relationPairs.reserve(content.relations.size());
        for (const ContentRelation& relation : content.relations)
        {
            if (relation.sourceFactionID == NONE_FACTION_ID || relation.targetFactionID == NONE_FACTION_ID)
            {
                NC_LOG_ERROR("Skipped explicit client faction relation {0}->{1}: faction 0 must remain Neutral", relation.sourceFactionID, relation.targetFactionID);
                continue;
            }

            FactionIndex source = INVALID_FACTION_INDEX;
            FactionIndex target = INVALID_FACTION_INDEX;
            if (!runtime.TryGetFactionIndex(relation.sourceFactionID, source) || !runtime.TryGetFactionIndex(relation.targetFactionID, target))
            {
                NC_LOG_ERROR("Skipped client faction relation {0}->{1}: one or both faction IDs are unknown", relation.sourceFactionID, relation.targetFactionID);
                continue;
            }

            const u32 pair = MakePairKey(source, target);
            if (!relationPairs.insert(pair).second)
            {
                NC_LOG_ERROR("Skipped duplicate client faction relation {0}->{1}", relation.sourceFactionID, relation.targetFactionID);
                continue;
            }

            if (!IsValidReaction(relation.reaction))
            {
                NC_LOG_ERROR("Skipped client faction relation {0}->{1}: reaction {2} is invalid", relation.sourceFactionID, relation.targetFactionID, relation.reaction);
                continue;
            }

            SetRelation(runtime, source, target, static_cast<Reaction>(relation.reaction));
        }

        std::vector<const ContentStanding*> sortedStandings;
        sortedStandings.reserve(content.standings.size());
        for (const ContentStanding& standing : content.standings)
        {
            sortedStandings.push_back(&standing);
        }

        std::sort(sortedStandings.begin(), sortedStandings.end(), [](const auto* left, const auto* right)
        {
            return left->minimumValue != right->minimumValue ? left->minimumValue < right->minimumValue : left->id < right->id;
        });

        robin_hood::unordered_flat_set<StandingID> standingIDs;
        standingIDs.reserve(sortedStandings.size());
        Reaction previousReaction = Reaction::Hostile;
        i32 previousMinimum = 0;
        u16 previousSortOrder = 0;
        bool firstStanding = true;

        for (const ContentStanding* standing : sortedStandings)
        {
            if (!standingIDs.insert(standing->id).second)
            {
                NC_LOG_ERROR("Skipped duplicate client faction standing ID {0}", standing->id);
                continue;
            }

            if (!IsValidReaction(standing->reaction))
            {
                NC_LOG_ERROR("Skipped client faction standing {0}: reaction {1} is invalid", standing->id, standing->reaction);
                continue;
            }

            if (!firstStanding && standing->minimumValue <= previousMinimum)
            {
                NC_LOG_ERROR("Skipped client faction standing {0}: minimum value {1} is not strictly greater than {2}", standing->id, standing->minimumValue, previousMinimum);
                continue;
            }

            if (!firstStanding && standing->sortOrder <= previousSortOrder)
            {
                NC_LOG_ERROR("Skipped client faction standing {0}: sort order {1} is not greater than {2}", standing->id, standing->sortOrder, previousSortOrder);
                continue;
            }

            const Reaction reaction = static_cast<Reaction>(standing->reaction);
            if (!firstStanding && static_cast<u8>(reaction) < static_cast<u8>(previousReaction))
            {
                NC_LOG_ERROR("Skipped client faction standing {0}: reaction ordering is not monotonic", standing->id);
                continue;
            }

            runtime.standingThresholds.push_back({
                .id = standing->id,
                .name = standing->name,
                .minimumValue = standing->minimumValue,
                .reaction = reaction,
                .sortOrder = standing->sortOrder
            });

            previousMinimum = standing->minimumValue;
            previousSortOrder = standing->sortOrder;
            previousReaction = reaction;
            firstStanding = false;
        }

        if (runtime.standingThresholds.empty())
        {
            if (anyFactionAllowsReputation)
            {
                NC_LOG_ERROR("Client reputation-enabled factions have no usable standing thresholds; all standing lookups will be Neutral");
            }
            ResetStandingToNeutral(runtime);
        }
        else if (runtime.standingThresholds.front().minimumValue > 0 || runtime.GetStanding(0).reaction != Reaction::Neutral)
        {
            NC_LOG_ERROR("Client faction standing thresholds do not map value 0 to Neutral; all standing lookups will be Neutral");
            ResetStandingToNeutral(runtime);
        }

        return std::make_shared<const FactionRuntimeData>(std::move(runtime));
    }
} // namespace Gameplay::Faction
