#include "FactionUtil.h"

#include "Game-Lib/ECS/Components/UnitFaction.h"

#include <Base/Util/DebugHandler.h>

#include <algorithm>
#include <limits>

namespace ECS::Util::Faction
{
    using namespace Gameplay::Faction;
    namespace State = Gameplay::Faction;

    namespace
    {
        constexpr u32 INVALID_BUCKET_SLOT = std::numeric_limits<u32>::max();

        bool HasPerceptionField(u8 fields, State::PerceptionOverrideFields field)
        {
            return (fields & static_cast<u8>(field)) != 0;
        }

        const State::ReputationState* FindReputation(const State::FactionState& state, FactionIndex factionIndex)
        {
            const auto itr = state.reputationByFaction.find(factionIndex);
            return itr != state.reputationByFaction.end() ? &itr->second : nullptr;
        }

        const State::PerceptionOverride* FindPerception(const State::FactionState& state, FactionIndex factionIndex)
        {
            const auto itr = state.perceptionByFaction.find(factionIndex);
            return itr != state.perceptionByFaction.end() ? &itr->second : nullptr;
        }

        StandingID GetEffectiveStandingID(const State::FactionState& state, FactionIndex factionIndex)
        {
            const FactionRuntimeData& runtime = *state.runtime;
            if (const State::PerceptionOverride* perception = FindPerception(state, factionIndex))
            {
                if (HasPerceptionField(perception->activeFields, State::PerceptionOverrideFields::Standing))
                    return runtime.GetStanding(perception->effectiveStandingValue).id;
            }

            const State::ReputationState* reputation = FindReputation(state, factionIndex);
            return runtime.GetStanding(reputation ? reputation->value : 0).id;
        }

        Reaction LocalToUnit(const State::FactionState& state, FactionIndex unitFaction)
        {
            const FactionRuntimeData& runtime = *state.runtime;
            if (state.localPlayerFaction == NONE_FACTION_INDEX || unitFaction == NONE_FACTION_INDEX)
                return Reaction::Neutral;

            if (const State::PerceptionOverride* perception = FindPerception(state, unitFaction))
            {
                if (HasPerceptionField(perception->activeFields, State::PerceptionOverrideFields::Reaction))
                    return perception->effectiveReaction;

                if (HasPerceptionField(perception->activeFields, State::PerceptionOverrideFields::Standing))
                    return runtime.GetStanding(perception->effectiveStandingValue).reaction;
            }

            if (HasFlag(runtime.GetDefinition(unitFaction).flags, DefinitionFlags::AllowsReputation))
            {
                if (const State::ReputationState* reputation = FindReputation(state, unitFaction))
                    return runtime.GetStanding(reputation->value).reaction;
            }

            return runtime.GetRelation(state.localPlayerFaction, unitFaction);
        }

        Reaction UnitToLocal(const State::FactionState& state, const Components::UnitFaction& unitFaction)
        {
            const FactionRuntimeData& runtime = *state.runtime;
            if (state.localPlayerFaction == NONE_FACTION_INDEX || unitFaction.factionIndex == NONE_FACTION_INDEX)
                return Reaction::Neutral;

            Reaction reaction = runtime.GetRelation(unitFaction.factionIndex, state.localPlayerFaction);
            if (HasFlag(runtime.GetDefinition(unitFaction.factionIndex).flags, DefinitionFlags::AllowsReputation))
            {
                if (const State::ReputationState* reputation = FindReputation(state, unitFaction.factionIndex))
                    reaction = runtime.GetStanding(reputation->value).reaction;
            }

            const ReactionBounds bounds = ReactionBounds::IsValidPacked(unitFaction.playerReactionBounds)
                ? ReactionBounds::Unpack(unitFaction.playerReactionBounds)
                : ReactionBounds::Unpack(NEUTRAL_REACTION_BOUNDS);

            return bounds.Clamp(reaction);
        }

        Reaction Presentation(const State::FactionState& state, const Components::UnitFaction& unitFaction)
        {
            const FactionRuntimeData& runtime = *state.runtime;
            if (state.localPlayerFaction == NONE_FACTION_INDEX || unitFaction.factionIndex == NONE_FACTION_INDEX)
                return Reaction::Neutral;

            const State::PerceptionOverride* perception = FindPerception(state, unitFaction.factionIndex);
            Reaction reaction = runtime.GetRelation(unitFaction.factionIndex, state.localPlayerFaction);

            if (perception && HasPerceptionField(perception->activeFields, State::PerceptionOverrideFields::Standing))
            {
                reaction = runtime.GetStanding(perception->effectiveStandingValue).reaction;
            }
            else if (HasFlag(runtime.GetDefinition(unitFaction.factionIndex).flags, DefinitionFlags::AllowsReputation))
            {
                if (const State::ReputationState* reputation = FindReputation(state, unitFaction.factionIndex))
                    reaction = runtime.GetStanding(reputation->value).reaction;
            }

            const ReactionBounds bounds = ReactionBounds::IsValidPacked(unitFaction.playerReactionBounds)
                ? ReactionBounds::Unpack(unitFaction.playerReactionBounds)
                : ReactionBounds::Unpack(NEUTRAL_REACTION_BOUNDS);

            reaction = bounds.Clamp(reaction);

            if (perception && HasPerceptionField(perception->activeFields, State::PerceptionOverrideFields::Reaction))
                reaction = perception->effectiveReaction;

            return reaction;
        }

        bool HasAtWarFlag(const State::FactionState& state, FactionIndex targetFaction)
        {
            const FactionRuntimeData& runtime = *state.runtime;
            const DefinitionRuntime& definition = runtime.GetDefinition(targetFaction);
            if (!HasFlag(definition.flags, DefinitionFlags::AllowsReputation) || !HasFlag(definition.flags, DefinitionFlags::CanSetAtWar))
                return false;

            const State::ReputationState* reputation = FindReputation(state, targetFaction);
            return reputation && (reputation->flags & static_cast<u16>(ReputationFlags::AtWar)) != 0;
        }

        void RemoveFromBucket(entt::registry& registry, entt::entity entity, Components::UnitFaction& unitFaction)
        {
            if (unitFaction.bucketSlot == INVALID_BUCKET_SLOT)
                return;

            auto& state = registry.ctx().get<State::FactionState>();
            if (unitFaction.factionIndex >= state.unitsByFaction.size())
            {
                NC_LOG_ERROR("Client faction bucket index {0} is out of range during unit removal", unitFaction.factionIndex);
                unitFaction.bucketSlot = INVALID_BUCKET_SLOT;
                return;
            }

            std::vector<entt::entity>& bucket = state.unitsByFaction[unitFaction.factionIndex];
            if (unitFaction.bucketSlot >= bucket.size() || bucket[unitFaction.bucketSlot] != entity)
            {
                NC_LOG_ERROR("Client faction bucket slot {0} is invalid for entity {1}", unitFaction.bucketSlot, entt::to_integral(entity));
                unitFaction.bucketSlot = INVALID_BUCKET_SLOT;
                return;
            }

            const entt::entity swappedEntity = bucket.back();
            bucket[unitFaction.bucketSlot] = swappedEntity;
            bucket.pop_back();

            if (swappedEntity != entity)
            {
                if (Components::UnitFaction* swappedFaction = registry.try_get<Components::UnitFaction>(swappedEntity))
                {
                    swappedFaction->bucketSlot = unitFaction.bucketSlot;
                }
                else
                {
                    NC_LOG_ERROR("Client faction bucket contained entity {0} without a faction component", entt::to_integral(swappedEntity));
                }
            }

            unitFaction.bucketSlot = INVALID_BUCKET_SLOT;
        }

        void InsertIntoBucket(entt::registry& registry, entt::entity entity, Components::UnitFaction& unitFaction)
        {
            auto& state = registry.ctx().get<State::FactionState>();
            if (unitFaction.factionIndex >= state.unitsByFaction.size())
                unitFaction.factionIndex = NONE_FACTION_INDEX;

            std::vector<entt::entity>& bucket = state.unitsByFaction[unitFaction.factionIndex];
            unitFaction.bucketSlot = static_cast<u32>(bucket.size());
            bucket.push_back(entity);
        }

        bool ResolveFactionIndex(State::FactionState& state, FactionID factionID, FactionIndex& factionIndex)
        {
            if (state.runtime && state.runtime->TryGetFactionIndex(factionID, factionIndex))
                return true;

            factionIndex = NONE_FACTION_INDEX;
            if (state.runtime && state.reportedUnknownFactionIDs.insert(factionID).second)
            {
                NC_LOG_ERROR("Received unknown stable faction ID {0}; None/Neutral will be used", factionID);
            }

            return false;
        }

        void OnUnitFactionDestroyed(entt::registry& registry, entt::entity entity)
        {
            auto* state = registry.ctx().find<State::FactionState>();
            Components::UnitFaction* unitFaction = registry.try_get<Components::UnitFaction>(entity);
            if (!state || !unitFaction)
                return;

            RemoveFromBucket(registry, entity, *unitFaction);
            if (state->localPlayer == entity)
            {
                state->localPlayer = entt::null;
                state->localPlayerFaction = NONE_FACTION_INDEX;
            }
        }

        State::FactionState& GetOrCreateState(entt::registry& registry)
        {
            auto* state = registry.ctx().find<State::FactionState>();
            if (!state)
            {
                state = &registry.ctx().emplace<State::FactionState>();
                registry.on_destroy<Components::UnitFaction>().connect<&OnUnitFactionDestroyed>();
            }

            return *state;
        }
    } // namespace

    void Initialize(entt::registry& registry, std::shared_ptr<const FactionRuntimeData> runtime)
    {
        if (!runtime || runtime->definitions.empty() || runtime->standingThresholds.empty())
        {
            NC_LOG_ERROR("Cannot initialize client faction state without complete compiled runtime data");
            return;
        }

        State::FactionState& state = GetOrCreateState(registry);
        const bool runtimeChanged = state.runtime && state.runtime != runtime && (!state.reputationByFaction.empty() || !state.perceptionByFaction.empty());

        state.runtime = std::move(runtime);
        state.reportedUnknownFactionIDs.clear();
        state.unitsByFaction.clear();
        state.unitsByFaction.resize(state.runtime->definitions.size());

        if (runtimeChanged)
        {
            NC_LOG_WARNING("Client faction content was replaced; owner reputation and perception state were reset");
            state.reputationByFaction.clear();
            state.perceptionByFaction.clear();
        }

        registry.view<Components::UnitFaction>().each([&](entt::entity entity, Components::UnitFaction& unitFaction)
        {
            FactionIndex index = NONE_FACTION_INDEX;
            if (!ResolveFactionIndex(state, unitFaction.factionID, index))
                unitFaction.factionID = NONE_FACTION_ID;

            unitFaction.factionIndex = index;
            unitFaction.bucketSlot = INVALID_BUCKET_SLOT;
            InsertIntoBucket(registry, entity, unitFaction);
        });

        if (registry.valid(state.localPlayer))
        {
            if (const Components::UnitFaction* localFaction = registry.try_get<Components::UnitFaction>(state.localPlayer))
                state.localPlayerFaction = localFaction->factionIndex;
        }

        RecalculateAll(registry);
    }

    void SetEventCallbacks(entt::registry& registry, State::UnitReactionChangedCallback unitReactionChanged, State::ReputationChangedCallback reputationChanged)
    {
        State::FactionState& state = GetOrCreateState(registry);
        state.unitReactionChanged = unitReactionChanged;
        state.reputationChanged = reputationChanged;
    }

    bool AttachUnit(entt::registry& registry, entt::entity entity)
    {
        const auto* state = registry.ctx().find<State::FactionState>();
        if (!state || !state->runtime || !registry.valid(entity) || registry.all_of<Components::UnitFaction>(entity))
            return false;

        Components::UnitFaction& unitFaction = registry.emplace<Components::UnitFaction>(entity);
        InsertIntoBucket(registry, entity, unitFaction);
        return true;
    }

    bool DetachUnit(entt::registry& registry, entt::entity entity)
    {
        Components::UnitFaction* unitFaction = registry.try_get<Components::UnitFaction>(entity);
        if (!unitFaction)
            return false;

        RemoveFromBucket(registry, entity, *unitFaction);
        registry.remove<Components::UnitFaction>(entity);
        return true;
    }

    bool ApplyUnitUpdate(entt::registry& registry, entt::entity entity, FactionID factionID, u8 playerReactionBounds)
    {
        auto* state = registry.ctx().find<State::FactionState>();
        if (!state || !state->runtime)
            return false;

        Components::UnitFaction* unitFaction = registry.try_get<Components::UnitFaction>(entity);
        if (!unitFaction)
        {
            if (!AttachUnit(registry, entity))
                return false;

            unitFaction = registry.try_get<Components::UnitFaction>(entity);
        }

        FactionIndex newIndex = NONE_FACTION_INDEX;
        const bool knownFaction = ResolveFactionIndex(*state, factionID, newIndex);
        const FactionID resolvedID = knownFaction ? factionID : NONE_FACTION_ID;

        u8 resolvedBounds = playerReactionBounds;
        if (!ReactionBounds::IsValidPacked(resolvedBounds))
        {
            NC_LOG_ERROR("Received invalid packed player reaction bounds {0} for faction {1}; Neutral..Neutral will be used", resolvedBounds, factionID);
            resolvedBounds = NEUTRAL_REACTION_BOUNDS;
        }

        const FactionIndex oldIndex = unitFaction->factionIndex;
        const bool factionChanged = oldIndex != newIndex || unitFaction->factionID != resolvedID;
        const bool boundsChanged = unitFaction->playerReactionBounds != resolvedBounds;
        if (!factionChanged && !boundsChanged)
            return false;

        if (oldIndex != newIndex)
        {
            RemoveFromBucket(registry, entity, *unitFaction);
            unitFaction->factionIndex = newIndex;
            InsertIntoBucket(registry, entity, *unitFaction);
        }

        unitFaction->factionID = resolvedID;
        unitFaction->playerReactionBounds = resolvedBounds;

        if (state->localPlayer == entity && oldIndex != newIndex)
        {
            state->localPlayerFaction = newIndex;
            RecalculateAll(registry);
        }
        else
        {
            RecalculateEntity(registry, entity);
        }

        return true;
    }

    bool ApplyReputationUpdate(entt::registry& registry, FactionID factionID, i32 value, u16 flags, bool isPresent)
    {
        auto* state = registry.ctx().find<State::FactionState>();
        if (!state || !state->runtime)
            return false;

        FactionIndex factionIndex = NONE_FACTION_INDEX;
        if (!ResolveFactionIndex(*state, factionID, factionIndex) || factionIndex == NONE_FACTION_INDEX)
            return false;

        const FactionRuntimeData& runtime = *state->runtime;
        if (!HasFlag(runtime.GetDefinition(factionIndex).flags, DefinitionFlags::AllowsReputation))
        {
            NC_LOG_ERROR("Received reputation state for faction {0}, which does not allow reputation", factionID);
            return false;
        }

        const auto oldItr = state->reputationByFaction.find(factionIndex);
        const bool wasPresent = oldItr != state->reputationByFaction.end();
        const State::ReputationState oldState = wasPresent ? oldItr->second : State::ReputationState{};
        const StandingID oldEffectiveStandingID = GetEffectiveStandingID(*state, factionIndex);
        const State::PerceptionOverride* perception = FindPerception(*state, factionIndex);
        const u8 perceptionFields = perception ? perception->activeFields : 0;

        State::ReputationState newState{};
        if (isPresent)
        {
            if ((flags & ~REPUTATION_FLAG_MASK) != 0)
            {
                NC_LOG_ERROR("Received unknown reputation flags {0} for faction {1}; unknown flags are ignored", flags & ~REPUTATION_FLAG_MASK, factionID);
                flags &= REPUTATION_FLAG_MASK;
            }

            newState = { .value = value, .flags = flags };
            if (wasPresent && oldState == newState)
                return false;

            state->reputationByFaction.insert_or_assign(factionIndex, newState);
        }
        else
        {
            if (!wasPresent)
                return false;

            state->reputationByFaction.erase(factionIndex);
        }

        RecalculateFactionBucket(registry, factionIndex);
        if (state->reputationChanged)
        {
            state->reputationChanged({
                .factionID = factionID,
                .oldValue = oldState.value,
                .newValue = newState.value,
                .oldFlags = oldState.flags,
                .newFlags = newState.flags,
                .oldPersistentStandingID = runtime.GetStanding(oldState.value).id,
                .newPersistentStandingID = runtime.GetStanding(newState.value).id,
                .oldEffectiveStandingID = oldEffectiveStandingID,
                .newEffectiveStandingID = GetEffectiveStandingID(*state, factionIndex),
                .oldPerceptionFields = perceptionFields,
                .newPerceptionFields = perceptionFields,
                .wasPresent = wasPresent,
                .isPresent = isPresent
            });
        }

        return true;
    }

    bool ApplyPerceptionUpdate(entt::registry& registry, FactionID factionID, u8 activeFields, i32 effectiveStandingValue, u8 effectiveReaction)
    {
        auto* state = registry.ctx().find<State::FactionState>();
        if (!state || !state->runtime)
            return false;

        FactionIndex factionIndex = NONE_FACTION_INDEX;
        if (!ResolveFactionIndex(*state, factionID, factionIndex) || factionIndex == NONE_FACTION_INDEX)
            return false;

        const u8 unknownFields = activeFields & ~State::PERCEPTION_OVERRIDE_FIELD_MASK;
        if (unknownFields != 0)
        {
            NC_LOG_ERROR("Received unknown faction perception fields {0} for faction {1}; unknown fields are ignored", unknownFields, factionID);
            activeFields &= State::PERCEPTION_OVERRIDE_FIELD_MASK;
        }

        if (HasPerceptionField(activeFields, State::PerceptionOverrideFields::Reaction) && !IsValidReaction(effectiveReaction))
        {
            NC_LOG_ERROR("Received invalid faction perception reaction {0} for faction {1}; the reaction override is ignored", effectiveReaction, factionID);
            activeFields &= ~static_cast<u8>(State::PerceptionOverrideFields::Reaction);
        }

        if (!HasPerceptionField(activeFields, State::PerceptionOverrideFields::Standing))
            effectiveStandingValue = 0;

        if (!HasPerceptionField(activeFields, State::PerceptionOverrideFields::Reaction))
            effectiveReaction = static_cast<u8>(Reaction::Neutral);

        const auto oldItr = state->perceptionByFaction.find(factionIndex);
        const bool hadPerception = oldItr != state->perceptionByFaction.end();
        const State::PerceptionOverride oldPerception = hadPerception ? oldItr->second : State::PerceptionOverride{};
        const StandingID oldEffectiveStandingID = GetEffectiveStandingID(*state, factionIndex);

        const State::ReputationState* reputation = FindReputation(*state, factionIndex);
        const bool reputationPresent = reputation != nullptr;
        const State::ReputationState reputationState = reputationPresent ? *reputation : State::ReputationState{};

        if (activeFields == 0)
        {
            if (!hadPerception)
                return false;

            state->perceptionByFaction.erase(factionIndex);
        }
        else
        {
            const State::PerceptionOverride newPerception{
                .activeFields = activeFields,
                .effectiveStandingValue = effectiveStandingValue,
                .effectiveReaction = static_cast<Reaction>(effectiveReaction)
            };
            if (hadPerception && oldPerception == newPerception)
                return false;

            state->perceptionByFaction.insert_or_assign(factionIndex, newPerception);
        }

        RecalculateFactionBucket(registry, factionIndex);
        if (state->reputationChanged)
        {
            const StandingID persistentStandingID = state->runtime->GetStanding(reputationState.value).id;
            state->reputationChanged({
                .factionID = factionID,
                .oldValue = reputationState.value,
                .newValue = reputationState.value,
                .oldFlags = reputationState.flags,
                .newFlags = reputationState.flags,
                .oldPersistentStandingID = persistentStandingID,
                .newPersistentStandingID = persistentStandingID,
                .oldEffectiveStandingID = oldEffectiveStandingID,
                .newEffectiveStandingID = GetEffectiveStandingID(*state, factionIndex),
                .oldPerceptionFields = oldPerception.activeFields,
                .newPerceptionFields = activeFields,
                .wasPresent = reputationPresent,
                .isPresent = reputationPresent
            });
        }

        return true;
    }

    void SetLocalPlayer(entt::registry& registry, entt::entity entity)
    {
        auto& state = registry.ctx().get<State::FactionState>();
        state.localPlayer = entt::null;
        state.localPlayerFaction = NONE_FACTION_INDEX;

        if (registry.valid(entity))
        {
            if (const Components::UnitFaction* unitFaction = registry.try_get<Components::UnitFaction>(entity))
            {
                state.localPlayer = entity;
                state.localPlayerFaction = unitFaction->factionIndex;
            }
        }

        RecalculateAll(registry);
    }

    void ResetOwnerState(entt::registry& registry)
    {
        auto& state = registry.ctx().get<State::FactionState>();
        state.reputationByFaction.clear();
        state.perceptionByFaction.clear();
        state.localPlayer = entt::null;
        state.localPlayerFaction = NONE_FACTION_INDEX;

        RecalculateAll(registry);
    }

    Reaction GetLocalReactionToUnit(const entt::registry& registry, entt::entity entity)
    {
        const auto* state = registry.ctx().find<State::FactionState>();
        const Components::UnitFaction* unitFaction = registry.try_get<Components::UnitFaction>(entity);
        return state && state->runtime && unitFaction ? LocalToUnit(*state, unitFaction->factionIndex) : Reaction::Neutral;
    }

    Reaction GetUnitReactionToLocalPlayer(const entt::registry& registry, entt::entity entity)
    {
        const auto* state = registry.ctx().find<State::FactionState>();
        const Components::UnitFaction* unitFaction = registry.try_get<Components::UnitFaction>(entity);
        return state && state->runtime && unitFaction ? UnitToLocal(*state, *unitFaction) : Reaction::Neutral;
    }

    Reaction GetPresentationReaction(const entt::registry& registry, entt::entity entity)
    {
        const Components::UnitFaction* unitFaction = registry.try_get<Components::UnitFaction>(entity);
        return unitFaction ? unitFaction->presentationReaction : Reaction::Neutral;
    }

    bool CanAttack(const entt::registry& registry, entt::entity entity)
    {
        const auto* state = registry.ctx().find<State::FactionState>();
        if (!state || !state->runtime || !registry.valid(entity) || !registry.valid(state->localPlayer) || state->localPlayer == entity)
            return false;

        const auto* targetFaction = registry.try_get<Components::UnitFaction>(entity);
        if (!targetFaction || !registry.all_of<Components::UnitFaction>(state->localPlayer))
            return false;

        if (HasAtWarFlag(*state, targetFaction->factionIndex))
            return true;

        return LocalToUnit(*state, targetFaction->factionIndex) != Reaction::Friendly && UnitToLocal(*state, *targetFaction) != Reaction::Friendly;
    }

    const StandingThreshold* GetPersistentStanding(const entt::registry& registry, FactionID factionID)
    {
        const auto* state = registry.ctx().find<State::FactionState>();
        if (!state || !state->runtime)
            return nullptr;

        const FactionRuntimeData& runtime = *state->runtime;
        FactionIndex factionIndex = INVALID_FACTION_INDEX;
        if (!runtime.TryGetFactionIndex(factionID, factionIndex))
            return &runtime.GetStanding(0);

        const State::ReputationState* reputation = FindReputation(*state, factionIndex);
        return &runtime.GetStanding(reputation ? reputation->value : 0);
    }

    const StandingThreshold* GetEffectiveStanding(const entt::registry& registry, FactionID factionID)
    {
        const auto* state = registry.ctx().find<State::FactionState>();
        if (!state || !state->runtime)
            return nullptr;

        const FactionRuntimeData& runtime = *state->runtime;
        FactionIndex factionIndex = INVALID_FACTION_INDEX;
        if (!runtime.TryGetFactionIndex(factionID, factionIndex))
            return &runtime.GetStanding(0);

        if (const State::PerceptionOverride* perception = FindPerception(*state, factionIndex))
        {
            if (HasPerceptionField(perception->activeFields, State::PerceptionOverrideFields::Standing))
                return &runtime.GetStanding(perception->effectiveStandingValue);
        }

        return GetPersistentStanding(registry, factionID);
    }

    i32 GetPersistentReputationValue(const entt::registry& registry, FactionID factionID)
    {
        State::ReputationState reputation;
        return FindReputation(registry, factionID, reputation) ? reputation.value : 0;
    }

    bool FindReputation(const entt::registry& registry, FactionID factionID, State::ReputationState& reputation)
    {
        const auto* state = registry.ctx().find<State::FactionState>();
        if (!state || !state->runtime)
            return false;

        FactionIndex factionIndex = INVALID_FACTION_INDEX;
        if (!state->runtime->TryGetFactionIndex(factionID, factionIndex))
            return false;

        const State::ReputationState* found = FindReputation(*state, factionIndex);
        if (!found)
            return false;

        reputation = *found;
        return true;
    }

    bool RecalculateEntity(entt::registry& registry, entt::entity entity)
    {
        auto* state = registry.ctx().find<State::FactionState>();
        Components::UnitFaction* unitFaction = registry.try_get<Components::UnitFaction>(entity);
        if (!state || !state->runtime || !unitFaction)
            return false;

        const Reaction oldReaction = unitFaction->presentationReaction;
        const Reaction newReaction = Presentation(*state, *unitFaction);
        if (oldReaction == newReaction)
            return false;

        unitFaction->presentationReaction = newReaction;
        if (state->unitReactionChanged)
            state->unitReactionChanged(entity, oldReaction, newReaction);

        return true;
    }

    u32 RecalculateFactionBucket(entt::registry& registry, FactionIndex factionIndex)
    {
        auto* state = registry.ctx().find<State::FactionState>();
        if (!state || factionIndex >= state->unitsByFaction.size())
            return 0;

        const std::vector<entt::entity>& bucket = state->unitsByFaction[factionIndex];
        for (entt::entity entity : bucket)
        {
            RecalculateEntity(registry, entity);
        }

        return static_cast<u32>(bucket.size());
    }

    u32 RecalculateAll(entt::registry& registry)
    {
        u32 count = 0;
        for (entt::entity entity : registry.view<Components::UnitFaction>())
        {
            RecalculateEntity(registry, entity);
            ++count;
        }

        return count;
    }
} // namespace ECS::Util::Faction
