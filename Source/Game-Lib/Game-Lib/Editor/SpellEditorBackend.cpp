#include "SpellEditorBackend.h"

#include "Game-Lib/Application/EnttRegistries.h"
#include "Game-Lib/ECS/Singletons/NetworkState.h"
#include "Game-Lib/ECS/Util/Database/SpellUtil.h"
#include "Game-Lib/ECS/Util/MessageBuilderUtil.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Base/Memory/Bytebuffer.h>

#include <Gameplay/GameDefine.h>

#include <Network/Client.h>

#include <entt/entt.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>
#include <utility>

namespace Editor
{
    namespace
    {
        constexpr u32 MAX_PAYLOAD_SIZE = 1024 * 1024;
        constexpr size_t MAX_EFFECTS = 64;
        constexpr size_t MAX_CONSTRAINTS = 16;
        constexpr size_t MAX_PROC_LINKS = 16;
        constexpr u8 SPELL_SYNC_VERSION = 7;
        constexpr f32 INFINITE_AURA_DURATION = -1.0f;
        static_assert(MAX_EFFECTS <= std::numeric_limits<u8>::max());

        template <typename Meta>
        bool IsEnumValue(typename Meta::Type value, bool includeTerminal = false)
        {
            const size_t end = Meta::ENUM_FIELD_LIST.size() - (includeTerminal ? 0 : 1);
            for (size_t index = 0; index < end; ++index)
            {
                if (Meta::ENUM_FIELD_LIST[index].second == value)
                    return true;
            }

            return false;
        }

        template <typename Meta, typename Value>
        bool IsSupportedMaskValue(Value value)
        {
            Value knownBits = 0;
            std::optional<Value> allValue;
            for (const auto& [name, enumValue] : Meta::ENUM_FIELD_LIST)
            {
                if (name == "All")
                    allValue = static_cast<Value>(enumValue);
                else
                    knownBits |= static_cast<Value>(enumValue);
            }

            return (allValue && value == *allValue) ||
                   (value & ~knownBits) == 0;
        }

        template <typename T>
        T ClampNumber(f64 value)
        {
            if (!std::isfinite(value))
                return {};

            const f64 minimum = static_cast<f64>(std::numeric_limits<T>::lowest());
            const f64 maximum = static_cast<f64>(std::numeric_limits<T>::max());
            return static_cast<T>(std::clamp(value, minimum, maximum));
        }

        template <typename T>
        T* FindByID(std::vector<T>& values, u32 id)
        {
            const auto itr = std::ranges::find(values, id, &T::id);
            return itr != values.end() ? &*itr : nullptr;
        }

        template <typename T>
        const T* FindByID(const std::vector<T>& values, u32 id)
        {
            const auto itr = std::ranges::find(values, id, &T::id);
            return itr != values.end() ? &*itr : nullptr;
        }

        void NormalizeEffectPriorities(std::vector<SpellEditorEffectDraft>& effects)
        {
            for (size_t index = 0; index < effects.size(); ++index)
            {
                effects[index].priority = static_cast<u8>(MAX_EFFECTS - index);
            }
        }

        f32 NormalizeAuraDuration(f32 duration)
        {
            return duration == 0.0f || duration == INFINITE_AURA_DURATION
                ? INFINITE_AURA_DURATION
                : duration;
        }

        ECS::Singletons::NetworkState* GetNetworkState()
        {
            EnttRegistries* registries = ServiceLocator::GetEnttRegistries();
            if (!registries || !registries->gameRegistry)
                return nullptr;

            entt::registry::context& context = registries->gameRegistry->ctx();
            return context.contains<ECS::Singletons::NetworkState>()
                ? &context.get<ECS::Singletons::NetworkState>()
                : nullptr;
        }

        bool IsProcDataVisibleToSpell(const MetaGen::Shared::ClientDB::SpellProcDataRecord& procData, u32 spellID)
        {
            return procData.ownerSpellID == 0 || procData.ownerSpellID == spellID;
        }
    }

    SpellEditorData* SpellEditorBackend::GetData() const
    {
        EnttRegistries* registries = ServiceLocator::GetEnttRegistries();
        if (!registries || !registries->dbRegistry)
            return nullptr;

        entt::registry::context& context = registries->dbRegistry->ctx();
        return context.contains<SpellEditorData>() ? &context.get<SpellEditorData>() : nullptr;
    }

    bool SpellEditorBackend::RequestSnapshot()
    {
        ECS::Singletons::NetworkState* networkState = GetNetworkState();
        if (!networkState || !networkState->client || !networkState->client->IsConnected() || !networkState->isInWorld)
            return false;

        EnttRegistries* registries = ServiceLocator::GetEnttRegistries();
        entt::registry::context& context = registries->dbRegistry->ctx();
        SpellEditorData& data = context.contains<SpellEditorData>()
            ? context.get<SpellEditorData>()
            : context.emplace<SpellEditorData>();
        if (data.state == SpellEditorDataState::Loading)
            return true;

        const u32 requestID = data.StartRequest();
        std::shared_ptr<Bytebuffer> buffer = Bytebuffer::Borrow<64>();
        if (!ECS::Util::MessageBuilder::Cheat::BuildDatabaseEditorSnapshotRequest(buffer, MetaGen::Shared::DatabaseEditor::DatabaseEditorTypeEnum::Spell, requestID))
        {
            data.FailSnapshot(requestID);
            return false;
        }

        networkState->client->Send(buffer);
        return true;
    }

    void SpellEditorBackend::Update()
    {
        SpellEditorData* data = GetData();
        if (!data)
            return;

        if (_pendingRequestID != 0)
        {
            std::optional<SpellEditorMutationResult> result = data->TakeMutationResult(_pendingRequestID);
            if (result)
            {
                _pendingRequestID = 0;
                _serverDiagnostic = result->response;
                if (result->succeeded)
                {
                    _draftState = SpellEditorDraftState::Refreshing;
                    if (result->artifact == static_cast<u8>(MetaGen::Shared::Spell::SpellEditorArtifactEnum::Spell))
                    {
                        _refreshSpellID = result->artifactID;
                        _refreshDeletedSpell = result->mutationType == MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Delete;
                    }
                    else
                    {
                        _refreshSpellID = _draft ? _draft->spellID : 0;
                        _refreshDeletedSpell = false;
                    }
                    _mutationSuccess = true;
                    if (!RequestSnapshot())
                    {
                        _resumeDraftStateAfterRefresh.reset();
                        _draftState = SpellEditorDraftState::MutationFailed;
                        _serverDiagnostic = "Mutation succeeded, but the authoritative snapshot refresh could not be requested.";
                    }
                }
                else
                {
                    if (_resumeDraftStateAfterRefresh)
                    {
                        _draftState = *_resumeDraftStateAfterRefresh;
                        _resumeDraftStateAfterRefresh.reset();
                    }
                    else
                    {
                        _draftState = result->artifact == static_cast<u8>(MetaGen::Shared::Spell::SpellEditorArtifactEnum::Spell)
                            ? SpellEditorDraftState::MutationFailed
                            : (_draft ? SpellEditorDraftState::Clean : SpellEditorDraftState::None);
                    }
                }
            }
        }

        if (_draftState != SpellEditorDraftState::Refreshing)
            return;
        if (data->state == SpellEditorDataState::Failed)
        {
            _resumeDraftStateAfterRefresh.reset();
            _draftState = SpellEditorDraftState::MutationFailed;
            _serverDiagnostic = "Mutation succeeded, but refreshing the authoritative snapshot failed.";
            return;
        }
        if (data->state != SpellEditorDataState::Ready)
            return;

        if (_resumeDraftStateAfterRefresh)
        {
            _draftState = *_resumeDraftStateAfterRefresh;
            _resumeDraftStateAfterRefresh.reset();
            _diagnostics.clear();
            _serverDiagnostic.clear();
        }
        else if (_refreshDeletedSpell)
        {
            _draft.reset();
            _draftState = SpellEditorDraftState::None;
            _diagnostics.clear();
            _serverDiagnostic.clear();
        }
        else if (_refreshSpellID == 0)
        {
            _draftState = SpellEditorDraftState::None;
            _diagnostics.clear();
            _serverDiagnostic.clear();
        }
        else
        {
            SpellEditorDraft refreshedDraft;
            if (LoadDraft(_refreshSpellID, refreshedDraft))
            {
                _draft = std::move(refreshedDraft);
                _draftState = SpellEditorDraftState::Clean;
                _diagnostics.clear();
                _serverDiagnostic.clear();
            }
            else
            {
                _draftState = SpellEditorDraftState::MutationFailed;
                _serverDiagnostic = "The refreshed snapshot did not contain the mutated spell.";
            }
        }
    }

    bool SpellEditorBackend::LoadDraft(u32 spellID, SpellEditorDraft& draft) const
    {
        SpellEditorData* data = GetData();
        if (!data || data->state != SpellEditorDataState::Ready)
            return false;

        using Artifact = MetaGen::Shared::Spell::SpellEditorArtifactEnum;
        ::ClientDB::Data* spellStorage = data->GetStorage(Artifact::Spell);
        ::ClientDB::Data* auraStorage = data->GetStorage(Artifact::SpellAura);
        ::ClientDB::Data* effectStorage = data->GetStorage(Artifact::SpellEffects);
        ::ClientDB::Data* constraintStorage = data->GetStorage(Artifact::SpellAuraConstraint);
        ::ClientDB::Data* procLinkStorage = data->GetStorage(Artifact::SpellProcLink);
        if (!spellStorage || !spellStorage->Has(spellID))
            return false;

        const auto& spell = spellStorage->Get<MetaGen::Shared::ClientDB::SpellRecord>(spellID);
        draft = {};
        draft.spellID = spellID;
        draft.name = spellStorage->GetString(spell.name);
        draft.description = spellStorage->GetString(spell.description);
        draft.auraDescription = spellStorage->GetString(spell.auraDescription);
        draft.iconID = spell.iconID;
        draft.castTime = spell.castTime;
        draft.cooldown = spell.cooldown;
        draft.targetSelector = spell.targetSelector;
        draft.targetShape = spell.targetShape;
        draft.targetRelation = spell.targetRelation;
        draft.targetRecipientMask = spell.targetRecipientMask;
        draft.rangePolicy = spell.rangePolicy;
        draft.minimumRange = spell.minimumRange;
        draft.maximumRange = spell.maximumRange;
        draft.targetRadius = spell.targetRadius;
        draft.maximumTargets = spell.maximumTargets;

        if (auraStorage->Has(spellID))
        {
            const auto& aura = auraStorage->Get<MetaGen::Shared::ClientDB::SpellAuraRecord>(spellID);
            draft.aura = SpellEditorAuraDraft{
                .duration = NormalizeAuraDuration(aura.duration),
                .stacksPerApplication = aura.stacksPerApplication,
                .maximumStacks = aura.maximumStacks,
                .applicationPolicy = aura.applicationPolicy,
                .disposition = aura.disposition,
                .dispelType = aura.dispelType,
                .lifecycleFlags = aura.lifecycleFlags
            };
        }

        const std::vector<u32>* effectIDs = ECSUtil::Spell::GetSpellEffectList(data->spellIndex, spellID);
        if (effectIDs)
        {
            draft.effects.reserve(effectIDs->size());
            for (u32 effectID : *effectIDs)
            {
                if (!effectStorage->Has(effectID))
                    continue;
                const auto& effect = effectStorage->Get<MetaGen::Shared::ClientDB::SpellEffectsRecord>(effectID);
                draft.effects.push_back({ effectID, effect.effectPriority, effect.effectType, effect.parameters });
            }
        }
        NormalizeEffectPriorities(draft.effects);

        constraintStorage->Each([&](u32 constraintID, const MetaGen::Shared::ClientDB::SpellAuraConstraintRecord& constraint)
        {
            if (constraint.spellID == spellID)
            {
                draft.constraints.push_back({ constraintID, constraint.groupID, constraint.scope, constraint.maximumApplications, constraint.overflowBehavior, constraint.overrideMask });
            }

            return true;
        });
        std::ranges::sort(draft.constraints, {}, &SpellEditorConstraintDraft::id);

        procLinkStorage->Each([&](u32 procLinkID, const MetaGen::Shared::ClientDB::SpellProcLinkRecord& procLink)
        {
            if (procLink.spellID == spellID)
            {
                draft.procLinks.push_back({ procLinkID, procLink.procDataID, procLink.effectMask });
            }

            return true;
        });
        std::ranges::sort(draft.procLinks, {}, &SpellEditorProcLinkDraft::id);
        return true;
    }

    bool SpellEditorBackend::OpenDraft(u32 spellID)
    {
        if (_pendingRequestID != 0 || _draftState == SpellEditorDraftState::Refreshing)
            return false;

        SpellEditorDraft draft;
        if (!LoadDraft(spellID, draft))
            return false;

        _draft = std::move(draft);
        _draftState = SpellEditorDraftState::Clean;
        _diagnostics.clear();
        _serverDiagnostic.clear();
        return true;
    }

    bool SpellEditorBackend::CreateDraft()
    {
        if (_pendingRequestID != 0 || _draftState == SpellEditorDraftState::Refreshing)
            return false;

        SpellEditorData* data = GetData();
        if (!data || data->state != SpellEditorDataState::Ready)
            return false;

        _draft = SpellEditorDraft{};
        _draft->spellID = AllocateID(MetaGen::Shared::Spell::SpellEditorArtifactEnum::Spell);
        _draft->isCreate = true;
        _draft->name = "New Spell";
        _draft->targetSelector = static_cast<u8>(MetaGen::Shared::Spell::SpellTargetSelectorEnum::CasterTarget);
        _draft->targetShape = static_cast<u8>(MetaGen::Shared::Spell::SpellTargetShapeEnum::Single);
        _draft->targetRelation = static_cast<u8>(MetaGen::Shared::Spell::SpellTargetRelationEnum::Any);
        _draft->targetRecipientMask = static_cast<u8>(MetaGen::Shared::Spell::SpellTargetRecipientMaskEnum::Any);
        _draft->rangePolicy = static_cast<u8>(MetaGen::Shared::Spell::SpellRangePolicyEnum::None);
        _draftState = SpellEditorDraftState::Dirty;
        _diagnostics.clear();
        _serverDiagnostic.clear();
        return _draft->spellID != 0;
    }

    bool SpellEditorBackend::DuplicateDraft(u32 spellID)
    {
        if (_pendingRequestID != 0 || _draftState == SpellEditorDraftState::Refreshing)
            return false;

        SpellEditorDraft duplicate;
        if (!LoadDraft(spellID, duplicate))
            return false;

        duplicate.spellID = AllocateID(MetaGen::Shared::Spell::SpellEditorArtifactEnum::Spell);
        duplicate.isCreate = true;
        duplicate.name += " Copy";

        std::vector<u32> allocatedEffectIDs;
        for (SpellEditorEffectDraft& effect : duplicate.effects)
        {
            effect.id = AllocateID(MetaGen::Shared::Spell::SpellEditorArtifactEnum::SpellEffects, allocatedEffectIDs);
            allocatedEffectIDs.push_back(effect.id);
        }

        std::vector<u32> allocatedConstraintIDs;
        for (SpellEditorConstraintDraft& constraint : duplicate.constraints)
        {
            constraint.id = AllocateID(MetaGen::Shared::Spell::SpellEditorArtifactEnum::SpellAuraConstraint, allocatedConstraintIDs);
            allocatedConstraintIDs.push_back(constraint.id);
        }

        SpellEditorData* data = GetData();
        ::ClientDB::Data* procDataStorage = data
            ? data->GetStorage(MetaGen::Shared::Spell::SpellEditorArtifactEnum::SpellProcData)
            : nullptr;
        std::erase_if(duplicate.procLinks, [procDataStorage](const SpellEditorProcLinkDraft& procLink)
        {
            if (!procDataStorage || !procDataStorage->Has(procLink.procDataID))
                return true;

            const auto& procData =
                procDataStorage->Get<MetaGen::Shared::ClientDB::SpellProcDataRecord>(procLink.procDataID);
            return procData.ownerSpellID != 0;
        });

        std::vector<u32> allocatedProcLinkIDs;
        for (SpellEditorProcLinkDraft& procLink : duplicate.procLinks)
        {
            procLink.id = AllocateID(MetaGen::Shared::Spell::SpellEditorArtifactEnum::SpellProcLink, allocatedProcLinkIDs);
            allocatedProcLinkIDs.push_back(procLink.id);
        }

        _draft = std::move(duplicate);
        _draftState = SpellEditorDraftState::Dirty;
        _diagnostics.clear();
        _serverDiagnostic.clear();
        return _draft->spellID != 0;
    }

    void SpellEditorBackend::DiscardDraft()
    {
        if (_pendingRequestID != 0 || _draftState == SpellEditorDraftState::Refreshing)
            return;

        _draft.reset();
        _draftState = SpellEditorDraftState::None;
        _diagnostics.clear();
        _serverDiagnostic.clear();
    }

    void SpellEditorBackend::MarkDirty()
    {
        if (_draftState != SpellEditorDraftState::Synchronizing && _draftState != SpellEditorDraftState::Refreshing)
            _draftState = SpellEditorDraftState::Dirty;
        _serverDiagnostic.clear();
        _diagnostics.clear();
    }

    bool SpellEditorBackend::CanEdit() const
    {
        return _draft && _pendingRequestID == 0 && _draftState != SpellEditorDraftState::Refreshing;
    }

    bool SpellEditorBackend::SetSpellString(std::string_view field, std::string value)
    {
        if (!CanEdit() || value.size() > 4096)
            return false;

        if (field == "name")
            _draft->name = std::move(value);
        else if (field == "description")
            _draft->description = std::move(value);
        else if (field == "auraDescription")
            _draft->auraDescription = std::move(value);
        else
            return false;

        MarkDirty();
        return true;
    }

    bool SpellEditorBackend::SetSpellNumber(std::string_view field, f64 value)
    {
        if (!CanEdit() || !std::isfinite(value))
            return false;

        if (field == "spellID" && _draft->isCreate)
            _draft->spellID = ClampNumber<u32>(value);
        else if (field == "iconID")
            _draft->iconID = ClampNumber<u32>(value);
        else if (field == "castTime")
            _draft->castTime = ClampNumber<f32>(value);
        else if (field == "cooldown")
            _draft->cooldown = ClampNumber<f32>(value);
        else if (field == "targetSelector")
            _draft->targetSelector = ClampNumber<u8>(value);
        else if (field == "targetShape")
            _draft->targetShape = ClampNumber<u8>(value);
        else if (field == "targetRelation")
            _draft->targetRelation = ClampNumber<u8>(value);
        else if (field == "targetRecipientMask")
            _draft->targetRecipientMask = ClampNumber<u8>(value);
        else if (field == "rangePolicy")
            _draft->rangePolicy = ClampNumber<u8>(value);
        else if (field == "minimumRange")
            _draft->minimumRange = ClampNumber<f32>(value);
        else if (field == "maximumRange")
            _draft->maximumRange = ClampNumber<f32>(value);
        else if (field == "targetRadius")
            _draft->targetRadius = ClampNumber<f32>(value);
        else if (field == "maximumTargets")
            _draft->maximumTargets = ClampNumber<u16>(value);
        else
            return false;

        MarkDirty();
        return true;
    }

    bool SpellEditorBackend::SetAuraEnabled(bool enabled)
    {
        if (!CanEdit())
            return false;
        if (enabled && !_draft->aura)
        {
            _draft->aura = SpellEditorAuraDraft{};
        }
        else if (!enabled)
        {
            _draft->aura.reset();
            _draft->constraints.clear();
        }
        MarkDirty();
        return true;
    }

    bool SpellEditorBackend::SetAuraNumber(std::string_view field, f64 value)
    {
        if (!CanEdit() || !_draft->aura || !std::isfinite(value))
            return false;

        SpellEditorAuraDraft& aura = *_draft->aura;
        if (field == "duration")
            aura.duration = NormalizeAuraDuration(ClampNumber<f32>(value));
        else if (field == "stacksPerApplication")
            aura.stacksPerApplication = ClampNumber<u16>(value);
        else if (field == "maximumStacks")
            aura.maximumStacks = ClampNumber<u16>(value);
        else if (field == "applicationPolicy")
            aura.applicationPolicy = ClampNumber<u8>(value);
        else if (field == "disposition")
            aura.disposition = ClampNumber<u8>(value);
        else if (field == "dispelType")
            aura.dispelType = ClampNumber<u8>(value);
        else if (field == "lifecycleFlags")
            aura.lifecycleFlags = ClampNumber<u8>(value);
        else
            return false;

        MarkDirty();
        return true;
    }

    u32 SpellEditorBackend::AddEffect(u8 type)
    {
        if (!CanEdit() || _draft->effects.size() >= MAX_EFFECTS)
            return 0;

        const auto* descriptor = MetaGen::Shared::Spell::GetSpellEffectDescriptor(static_cast<MetaGen::Shared::Spell::SpellEffectTypeEnum>(type));
        if (!descriptor || descriptor->owner == MetaGen::Shared::Spell::SpellEffectOwner::None)
            return 0;

        std::vector<u32> draftIDs;
        draftIDs.reserve(_draft->effects.size());
        for (const SpellEditorEffectDraft& effect : _draft->effects)
        {
            draftIDs.push_back(effect.id);
        }

        SpellEditorEffectDraft effect;
        effect.id = AllocateID(MetaGen::Shared::Spell::SpellEditorArtifactEnum::SpellEffects, draftIDs);
        effect.type = type;
        for (u32 index = 0; index < descriptor->parameterCount; ++index)
        {
            effect.parameters[index] = descriptor->parameters[index].defaultValue;
        }
        _draft->effects.push_back(effect);
        NormalizeEffectPriorities(_draft->effects);
        MarkDirty();
        return effect.id;
    }

    bool SpellEditorBackend::RemoveEffect(u32 effectID)
    {
        if (!CanEdit())
            return false;
        const auto itr = std::ranges::find(_draft->effects, effectID, &SpellEditorEffectDraft::id);
        if (itr == _draft->effects.end())
            return false;

        const size_t effectIndex = static_cast<size_t>(std::distance(_draft->effects.begin(), itr));
        _draft->effects.erase(itr);
        for (SpellEditorProcLinkDraft& procLink : _draft->procLinks)
        {
            const u64 lowerMask = (u64{ 1 } << effectIndex) - 1;
            const u64 higherMask = effectIndex == 63 ? 0 : procLink.effectMask >> (effectIndex + 1);
            procLink.effectMask = (procLink.effectMask & lowerMask) | (higherMask << effectIndex);
        }
        NormalizeEffectPriorities(_draft->effects);
        MarkDirty();
        return true;
    }

    bool SpellEditorBackend::MoveEffect(u32 effectID, i32 offset)
    {
        if (!CanEdit() || offset == 0)
            return false;

        const auto effectItr = std::ranges::find(_draft->effects, effectID, &SpellEditorEffectDraft::id);
        if (effectItr == _draft->effects.end())
            return false;

        const i64 effectIndex = std::distance(_draft->effects.begin(), effectItr);
        const i64 targetIndex = effectIndex + (offset < 0 ? -1 : 1);
        if (targetIndex < 0 || targetIndex >= static_cast<i64>(_draft->effects.size()))
            return false;

        const u64 effectMask = u64{ 1 } << effectIndex;
        const u64 targetMask = u64{ 1 } << targetIndex;
        for (SpellEditorProcLinkDraft& procLink : _draft->procLinks)
        {
            const bool effectSelected = (procLink.effectMask & effectMask) != 0;
            const bool targetSelected = (procLink.effectMask & targetMask) != 0;
            procLink.effectMask &= ~(effectMask | targetMask);
            if (effectSelected)
                procLink.effectMask |= targetMask;
            if (targetSelected)
                procLink.effectMask |= effectMask;
        }

        std::iter_swap(_draft->effects.begin() + effectIndex, _draft->effects.begin() + targetIndex);
        NormalizeEffectPriorities(_draft->effects);
        MarkDirty();
        return true;
    }

    bool SpellEditorBackend::SetEffectNumber(u32 effectID, std::string_view field, i64 value)
    {
        if (!CanEdit())
            return false;
        SpellEditorEffectDraft* effect = FindByID(_draft->effects, effectID);
        if (!effect)
            return false;

        if (field == "type")
        {
            const auto* descriptor = MetaGen::Shared::Spell::GetSpellEffectDescriptor(static_cast<MetaGen::Shared::Spell::SpellEffectTypeEnum>(value));
            if (!descriptor || descriptor->owner == MetaGen::Shared::Spell::SpellEffectOwner::None)
                return false;
            effect->type = static_cast<u8>(value);
            effect->parameters.fill(0);
            for (u32 index = 0; index < descriptor->parameterCount; ++index)
                effect->parameters[index] = descriptor->parameters[index].defaultValue;
        }
        else
        {
            return false;
        }

        MarkDirty();
        return true;
    }

    bool SpellEditorBackend::SetEffectParameter(u32 effectID, u8 parameterIndex, i32 value)
    {
        if (!CanEdit() || parameterIndex >= 6)
            return false;
        SpellEditorEffectDraft* effect = FindByID(_draft->effects, effectID);
        if (!effect)
            return false;
        effect->parameters[parameterIndex] = value;
        MarkDirty();
        return true;
    }

    u32 SpellEditorBackend::AddConstraint(u32 groupID)
    {
        if (!CanEdit() || !_draft->aura || groupID == 0 || _draft->constraints.size() >= MAX_CONSTRAINTS)
            return 0;

        using Artifact = MetaGen::Shared::Spell::SpellEditorArtifactEnum;
        SpellEditorData* data = GetData();
        ::ClientDB::Data* groups = data ? data->GetStorage(Artifact::SpellAuraConstraintGroup) : nullptr;
        if (!groups || !groups->Has(groupID) || std::ranges::find(_draft->constraints, groupID, &SpellEditorConstraintDraft::groupID) != _draft->constraints.end())
            return 0;

        const auto& group = groups->Get<MetaGen::Shared::ClientDB::SpellAuraConstraintGroupRecord>(groupID);
        std::vector<u32> draftIDs;
        for (const SpellEditorConstraintDraft& constraint : _draft->constraints)
            draftIDs.push_back(constraint.id);
        const u32 id = AllocateID(Artifact::SpellAuraConstraint, draftIDs);
        _draft->constraints.push_back({ id, groupID, group.defaultScope, group.defaultMaximumApplications, group.defaultOverflowBehavior, 0 });
        MarkDirty();
        return id;
    }

    bool SpellEditorBackend::RemoveConstraint(u32 constraintID)
    {
        if (!CanEdit())
            return false;
        const auto itr = std::ranges::find(_draft->constraints, constraintID, &SpellEditorConstraintDraft::id);
        if (itr == _draft->constraints.end())
            return false;
        _draft->constraints.erase(itr);
        MarkDirty();
        return true;
    }

    u32 SpellEditorBackend::CreateConstraintGroup(std::string name, u8 defaultScope, u16 defaultMaximumApplications, u8 defaultOverflowBehavior)
    {
        using Artifact = MetaGen::Shared::Spell::SpellEditorArtifactEnum;

        const u32 groupID = AllocateID(Artifact::SpellAuraConstraintGroup);
        if (groupID == 0 || !SendConstraintGroupMutation(groupID, name, defaultScope, defaultMaximumApplications, defaultOverflowBehavior, MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Create))
        {
            return 0;
        }

        return groupID;
    }

    bool SpellEditorBackend::UpdateConstraintGroup(u32 groupID, std::string name, u8 defaultScope, u16 defaultMaximumApplications, u8 defaultOverflowBehavior)
    {
        return SendConstraintGroupMutation(groupID, name, defaultScope, defaultMaximumApplications, defaultOverflowBehavior, MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Update);
    }

    bool SpellEditorBackend::DeleteConstraintGroup(u32 groupID)
    {
        using Artifact = MetaGen::Shared::Spell::SpellEditorArtifactEnum;

        SpellEditorData* data = GetData();
        ECS::Singletons::NetworkState* networkState = GetNetworkState();
        ::ClientDB::Data* groups = data ? data->GetStorage(Artifact::SpellAuraConstraintGroup) : nullptr;
        if (groupID == 0 || !data || data->state != SpellEditorDataState::Ready || !groups || !groups->Has(groupID) ||
            !networkState || !networkState->client || !networkState->client->IsConnected() ||
            !networkState->isInWorld || _pendingRequestID != 0 ||
            (_draftState != SpellEditorDraftState::None && _draftState != SpellEditorDraftState::Clean))
        {
            return false;
        }

        std::shared_ptr<Bytebuffer> payload = Bytebuffer::Borrow<sizeof(u32)>();
        if (!payload->PutU32(groupID))
            return false;

        const u32 requestID = data->StartMutationRequest();
        std::shared_ptr<Bytebuffer> buffer = Bytebuffer::Borrow<128>();
        if (!ECS::Util::MessageBuilder::Cheat::BuildDatabaseEditorMutation(buffer, MetaGen::Shared::DatabaseEditor::DatabaseEditorTypeEnum::Spell, static_cast<u8>(Artifact::SpellAuraConstraintGroup), MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Delete, requestID, payload->GetDataPointer(), static_cast<u32>(payload->writtenData)))
        {
            return false;
        }

        networkState->client->Send(buffer);
        _pendingRequestID = requestID;
        _draftState = SpellEditorDraftState::Synchronizing;
        _serverDiagnostic.clear();
        return true;
    }

    bool SpellEditorBackend::SetConstraintNumber(u32 constraintID, std::string_view field, u64 value)
    {
        if (!CanEdit())
            return false;

        using Artifact = MetaGen::Shared::Spell::SpellEditorArtifactEnum;
        using Override = MetaGen::Shared::Spell::AuraConstraintOverrideFlagsEnum;

        SpellEditorConstraintDraft* constraint = FindByID(_draft->constraints, constraintID);
        if (!constraint)
            return false;

        SpellEditorData* data = GetData();
        ::ClientDB::Data* groups = data ? data->GetStorage(Artifact::SpellAuraConstraintGroup) : nullptr;
        const MetaGen::Shared::ClientDB::SpellAuraConstraintGroupRecord* group =
            groups && groups->Has(constraint->groupID)
                ? &groups->Get<MetaGen::Shared::ClientDB::SpellAuraConstraintGroupRecord>(constraint->groupID)
                : nullptr;

        const auto SetOverrideState = [constraint](Override flag, bool matchesDefault)
        {
            if (matchesDefault)
                constraint->overrideMask &= ~static_cast<u8>(flag);
            else
                constraint->overrideMask |= static_cast<u8>(flag);
        };

        if (field == "scope")
        {
            constraint->scope = static_cast<u8>(std::min<u64>(value, std::numeric_limits<u8>::max()));
            SetOverrideState(Override::Scope, group && constraint->scope == group->defaultScope);
        }
        else if (field == "maximumApplications")
        {
            constraint->maximumApplications = static_cast<u16>(std::min<u64>(value, std::numeric_limits<u16>::max()));
            SetOverrideState(Override::MaximumApplications, group && constraint->maximumApplications == group->defaultMaximumApplications);
        }
        else if (field == "overflowBehavior")
        {
            constraint->overflowBehavior = static_cast<u8>(std::min<u64>(value, std::numeric_limits<u8>::max()));
            SetOverrideState(Override::OverflowBehavior, group && constraint->overflowBehavior == group->defaultOverflowBehavior);
        }
        else
            return false;
        MarkDirty();
        return true;
    }

    bool SpellEditorBackend::ResetConstraintField(u32 constraintID, std::string_view field)
    {
        if (!CanEdit())
            return false;

        using Artifact = MetaGen::Shared::Spell::SpellEditorArtifactEnum;
        using Override = MetaGen::Shared::Spell::AuraConstraintOverrideFlagsEnum;

        SpellEditorConstraintDraft* constraint = FindByID(_draft->constraints, constraintID);
        SpellEditorData* data = GetData();
        ::ClientDB::Data* groups = data ? data->GetStorage(Artifact::SpellAuraConstraintGroup) : nullptr;
        if (!constraint || !groups || !groups->Has(constraint->groupID))
            return false;

        const auto& group = groups->Get<MetaGen::Shared::ClientDB::SpellAuraConstraintGroupRecord>(constraint->groupID);
        if (field == "scope")
        {
            constraint->scope = group.defaultScope;
            constraint->overrideMask &= ~static_cast<u8>(Override::Scope);
        }
        else if (field == "maximumApplications")
        {
            constraint->maximumApplications = group.defaultMaximumApplications;
            constraint->overrideMask &= ~static_cast<u8>(Override::MaximumApplications);
        }
        else if (field == "overflowBehavior")
        {
            constraint->overflowBehavior = group.defaultOverflowBehavior;
            constraint->overrideMask &= ~static_cast<u8>(Override::OverflowBehavior);
        }
        else
        {
            return false;
        }

        MarkDirty();
        return true;
    }

    u32 SpellEditorBackend::CreateProcData(u32 ownerSpellID, std::string name, u32 phaseMask, u64 typeMask, u64 hitMask, u64 flags, f32 procsPerMinute, f32 chanceToProc, u32 internalCooldownMS, i32 charges)
    {
        using Artifact = MetaGen::Shared::Spell::SpellEditorArtifactEnum;

        const u32 procDataID = AllocateID(Artifact::SpellProcData);
        const GameDefine::Database::SpellProcData value = {
            procDataID, phaseMask, typeMask, hitMask, flags,
            procsPerMinute, chanceToProc, internalCooldownMS, charges
        };
        if (procDataID == 0 || !SendProcDataMutation(value, ownerSpellID, name, MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Create))
        {
            return 0;
        }

        return procDataID;
    }

    bool SpellEditorBackend::UpdateProcData(u32 procDataID, u32 ownerSpellID, std::string name, u32 phaseMask, u64 typeMask, u64 hitMask, u64 flags, f32 procsPerMinute, f32 chanceToProc, u32 internalCooldownMS, i32 charges)
    {
        const GameDefine::Database::SpellProcData value = {
            procDataID, phaseMask, typeMask, hitMask, flags,
            procsPerMinute, chanceToProc, internalCooldownMS, charges
        };
        return SendProcDataMutation(value, ownerSpellID, name, MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Update);
    }

    bool SpellEditorBackend::DeleteProcData(u32 procDataID)
    {
        using Artifact = MetaGen::Shared::Spell::SpellEditorArtifactEnum;

        SpellEditorData* data = GetData();
        ECS::Singletons::NetworkState* networkState = GetNetworkState();
        ::ClientDB::Data* procData = data ? data->GetStorage(Artifact::SpellProcData) : nullptr;
        if (procDataID == 0 || !data || data->state != SpellEditorDataState::Ready ||
            !procData || !procData->Has(procDataID) || !networkState || !networkState->client ||
            !networkState->client->IsConnected() || !networkState->isInWorld ||
            _pendingRequestID != 0 ||
            (_draftState != SpellEditorDraftState::None && _draftState != SpellEditorDraftState::Clean && _draftState != SpellEditorDraftState::Dirty))
        {
            return false;
        }

        std::shared_ptr<Bytebuffer> payload = Bytebuffer::Borrow<sizeof(u32)>();
        if (!payload->PutU32(procDataID))
            return false;

        const u32 requestID = data->StartMutationRequest();
        std::shared_ptr<Bytebuffer> buffer = Bytebuffer::Borrow<128>();
        if (!ECS::Util::MessageBuilder::Cheat::BuildDatabaseEditorMutation(buffer, MetaGen::Shared::DatabaseEditor::DatabaseEditorTypeEnum::Spell, static_cast<u8>(Artifact::SpellProcData), MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Delete, requestID, payload->GetDataPointer(), static_cast<u32>(payload->writtenData)))
        {
            return false;
        }

        networkState->client->Send(buffer);
        _resumeDraftStateAfterRefresh = _draftState;
        _pendingRequestID = requestID;
        _draftState = SpellEditorDraftState::Synchronizing;
        _serverDiagnostic.clear();
        return true;
    }

    u32 SpellEditorBackend::AddProcLink(u32 procDataID)
    {
        if (!CanEdit() || _draft->procLinks.size() >= MAX_PROC_LINKS)
            return 0;
        if (std::ranges::find(_draft->procLinks, procDataID, &SpellEditorProcLinkDraft::procDataID) != _draft->procLinks.end())
        {
            return 0;
        }

        SpellEditorData* data = GetData();
        ::ClientDB::Data* procDataStorage = data && data->state == SpellEditorDataState::Ready
            ? data->GetStorage(MetaGen::Shared::Spell::SpellEditorArtifactEnum::SpellProcData)
            : nullptr;
        if (!procDataStorage || !procDataStorage->Has(procDataID))
            return 0;
        const auto& procData = procDataStorage->Get<MetaGen::Shared::ClientDB::SpellProcDataRecord>(procDataID);
        if (!IsProcDataVisibleToSpell(procData, _draft->spellID))
            return 0;

        std::vector<u32> draftIDs;
        for (const SpellEditorProcLinkDraft& procLink : _draft->procLinks)
            draftIDs.push_back(procLink.id);
        const u32 id = AllocateID(MetaGen::Shared::Spell::SpellEditorArtifactEnum::SpellProcLink, draftIDs);
        _draft->procLinks.push_back({ id, procDataID, 0 });
        MarkDirty();
        return id;
    }

    bool SpellEditorBackend::RemoveProcLink(u32 procLinkID)
    {
        if (!CanEdit())
            return false;
        const auto itr = std::ranges::find(_draft->procLinks, procLinkID, &SpellEditorProcLinkDraft::id);
        if (itr == _draft->procLinks.end())
            return false;
        _draft->procLinks.erase(itr);
        MarkDirty();
        return true;
    }

    bool SpellEditorBackend::SetProcLinkProcData(u32 procLinkID, u32 procDataID)
    {
        if (!CanEdit())
            return false;
        SpellEditorData* data = GetData();
        ::ClientDB::Data* procDataStorage = data && data->state == SpellEditorDataState::Ready
            ? data->GetStorage(MetaGen::Shared::Spell::SpellEditorArtifactEnum::SpellProcData)
            : nullptr;
        if (!procDataStorage || !procDataStorage->Has(procDataID))
            return false;
        const auto& procData = procDataStorage->Get<MetaGen::Shared::ClientDB::SpellProcDataRecord>(procDataID);
        if (!IsProcDataVisibleToSpell(procData, _draft->spellID))
            return false;
        SpellEditorProcLinkDraft* procLink = FindByID(_draft->procLinks, procLinkID);
        if (!procLink)
            return false;
        const auto duplicateItr = std::ranges::find(_draft->procLinks, procDataID, &SpellEditorProcLinkDraft::procDataID);
        if (duplicateItr != _draft->procLinks.end() && duplicateItr->id != procLinkID)
            return false;
        procLink->procDataID = procDataID;
        MarkDirty();
        return true;
    }

    bool SpellEditorBackend::SetProcLinkEffectSelected(u32 procLinkID, u32 effectID, bool selected)
    {
        if (!CanEdit())
            return false;
        SpellEditorProcLinkDraft* procLink = FindByID(_draft->procLinks, procLinkID);
        const auto effectItr = std::ranges::find(_draft->effects, effectID, &SpellEditorEffectDraft::id);
        if (!procLink || effectItr == _draft->effects.end())
            return false;
        const size_t effectIndex = static_cast<size_t>(std::distance(_draft->effects.begin(), effectItr));
        const u64 mask = u64{ 1 } << effectIndex;
        procLink->effectMask = selected ? procLink->effectMask | mask : procLink->effectMask & ~mask;
        MarkDirty();
        return true;
    }

    const std::vector<SpellEditorDiagnostic>& SpellEditorBackend::Validate()
    {
        _diagnostics.clear();
        if (!_draft)
        {
            _diagnostics.push_back({ "spell", "No spell draft is open." });
            return _diagnostics;
        }

        SpellEditorData* data = GetData();
        if (!data || data->state != SpellEditorDataState::Ready)
        {
            _diagnostics.push_back({ "snapshot", "The authoritative spell snapshot is not ready." });
            return _diagnostics;
        }

        using namespace MetaGen::Shared::Spell;
        using Artifact = SpellEditorArtifactEnum;
        ::ClientDB::Data* spellStorage = data->GetStorage(Artifact::Spell);
        ::ClientDB::Data* auraStorage = data->GetStorage(Artifact::SpellAura);
        ::ClientDB::Data* effectStorage = data->GetStorage(Artifact::SpellEffects);
        ::ClientDB::Data* groupStorage = data->GetStorage(Artifact::SpellAuraConstraintGroup);
        ::ClientDB::Data* constraintStorage = data->GetStorage(Artifact::SpellAuraConstraint);
        ::ClientDB::Data* procDataStorage = data->GetStorage(Artifact::SpellProcData);
        ::ClientDB::Data* procLinkStorage = data->GetStorage(Artifact::SpellProcLink);

        if (_draft->spellID == 0)
            _diagnostics.push_back({ "overview.spellID", "Stable Spell ID must be greater than zero." });
        if (_draft->name.empty())
            _diagnostics.push_back({ "overview.name", "Name is required." });
        if (_draft->isCreate && spellStorage->Has(_draft->spellID))
            _diagnostics.push_back({ "overview.spellID", "Stable Spell ID already exists in the authoritative snapshot." });
        if (!_draft->isCreate && !spellStorage->Has(_draft->spellID))
            _diagnostics.push_back({ "overview.spellID", "The spell no longer exists in the authoritative snapshot." });
        if (!std::isfinite(_draft->castTime) || _draft->castTime < 0.0f)
            _diagnostics.push_back({ "overview.castTime", "Cast time must be a finite non-negative value." });
        if (!std::isfinite(_draft->cooldown) || _draft->cooldown < 0.0f)
            _diagnostics.push_back({ "overview.cooldown", "Cooldown must be a finite non-negative value." });
        if (!IsEnumValue<SpellTargetSelectorEnumMeta>(_draft->targetSelector))
            _diagnostics.push_back({ "targeting.targetSelector", "Target selector is not a generated enum value." });
        if (!IsEnumValue<SpellTargetShapeEnumMeta>(_draft->targetShape))
            _diagnostics.push_back({ "targeting.targetShape", "Target shape is not a generated enum value." });
        if (!IsEnumValue<SpellTargetRelationEnumMeta>(_draft->targetRelation))
            _diagnostics.push_back({ "targeting.targetRelation", "Target relation is not a generated enum value." });
        constexpr u8 validRecipientMask = static_cast<u8>(SpellTargetRecipientMaskEnum::Any);
        if (_draft->targetRecipientMask == 0 || (_draft->targetRecipientMask & ~validRecipientMask) != 0)
            _diagnostics.push_back({ "targeting.targetRecipientMask", "At least one supported recipient category must be selected." });
        if (!IsEnumValue<SpellRangePolicyEnumMeta>(_draft->rangePolicy))
            _diagnostics.push_back({ "targeting.rangePolicy", "Range policy is not a generated enum value." });
        if (!std::isfinite(_draft->minimumRange) || !std::isfinite(_draft->maximumRange) || _draft->minimumRange < 0.0f || _draft->maximumRange < _draft->minimumRange)
        {
            _diagnostics.push_back({ "targeting.range", "Range must be finite, non-negative, and maximum must not be below minimum." });
        }
        const auto selector = static_cast<SpellTargetSelectorEnum>(_draft->targetSelector);
        const auto shape = static_cast<SpellTargetShapeEnum>(_draft->targetShape);
        const auto relation = static_cast<SpellTargetRelationEnum>(_draft->targetRelation);
        const auto rangePolicy = static_cast<SpellRangePolicyEnum>(_draft->rangePolicy);
        if (rangePolicy != SpellRangePolicyEnum::Distance && (_draft->minimumRange != 0.0f || _draft->maximumRange != 0.0f))
            _diagnostics.push_back({ "targeting.range", "Only the Distance range policy may define minimum and maximum range." });
        if (shape == SpellTargetShapeEnum::Single && (_draft->targetRadius != 0.0f || _draft->maximumTargets != 1))
            _diagnostics.push_back({ "targeting.targetShape", "Single-target spells require radius 0 and exactly one maximum target." });
        if (shape == SpellTargetShapeEnum::Radius && (!std::isfinite(_draft->targetRadius) || _draft->targetRadius <= 0.0f || _draft->targetRadius > GameDefine::SpellTargeting::MAX_RADIUS || _draft->maximumTargets == 0 || _draft->maximumTargets > GameDefine::SpellTargeting::MAX_TARGETS))
        {
            _diagnostics.push_back({ "targeting.targetShape", "Radius targeting requires a positive bounded radius and 1-64 maximum targets." });
        }
        if (shape == SpellTargetShapeEnum::Radius && selector == SpellTargetSelectorEnum::None)
            _diagnostics.push_back({ "targeting.targetSelector", "Radius targeting requires an anchor." });
        if (selector == SpellTargetSelectorEnum::GroundPosition && (relation != SpellTargetRelationEnum::Any || rangePolicy != SpellRangePolicyEnum::Distance))
        {
            _diagnostics.push_back({ "targeting.targetSelector", "Ground-position targeting requires Any anchor relation and a Distance range policy." });
        }
        if (selector == SpellTargetSelectorEnum::Caster && relation == SpellTargetRelationEnum::Attackable)
            _diagnostics.push_back({ "targeting.targetRelation", "Caster targeting cannot satisfy an Attackable anchor relation." });

        if (_draft->effects.size() > MAX_EFFECTS)
            _diagnostics.push_back({ "effects", "A spell may contain at most 64 effects." });
        std::unordered_set<u32> effectIDs;
        for (size_t effectIndex = 0; effectIndex < _draft->effects.size(); ++effectIndex)
        {
            const SpellEditorEffectDraft& effect = _draft->effects[effectIndex];
            const std::string path = "effects." + std::to_string(effect.id);
            if (effect.id == 0 || !effectIDs.insert(effect.id).second)
                _diagnostics.push_back({ path, "Stable Effect ID is zero or duplicated in this draft." });
            if (effectStorage->Has(effect.id))
            {
                const auto& snapshotEffect = effectStorage->Get<MetaGen::Shared::ClientDB::SpellEffectsRecord>(effect.id);
                if ((_draft->isCreate || snapshotEffect.spellID != _draft->spellID))
                    _diagnostics.push_back({ path, "Stable Effect ID belongs to another authoritative spell." });
            }

            const auto* descriptor = GetSpellEffectDescriptor(static_cast<SpellEffectTypeEnum>(effect.type));
            if (!descriptor || descriptor->owner == SpellEffectOwner::None)
            {
                _diagnostics.push_back({ path, "Effect type is not present in the generated catalog." });
                continue;
            }
            if (descriptor->owner == SpellEffectOwner::Aura && !_draft->aura)
                _diagnostics.push_back({ path, "Aura-owned effects require an explicit aura definition." });
            if (descriptor->target.mode == SpellEffectTargetMode::Required && selector == SpellTargetSelectorEnum::None)
            {
                _diagnostics.push_back({ "targeting.targetSelector", "A target-requiring effect needs a spell target selector." });
            }
            if (descriptor->target.mode == SpellEffectTargetMode::Required && selector == SpellTargetSelectorEnum::GroundPosition && shape == SpellTargetShapeEnum::Single)
            {
                _diagnostics.push_back({ "targeting.targetShape", "A ground-position spell needs a Radius shape to resolve unit-targeted effects." });
            }

            const SpellEffectParameterValidationResult parameterValidation = ValidateSpellEffectParameters(*descriptor, effect.parameters);
            if (!parameterValidation.IsValid())
            {
                const std::string parameterPath = path + ".parameters." + std::to_string(parameterValidation.parameterIndex + 1);
                _diagnostics.push_back({ parameterPath, "Effect parameter failed generated catalog validation: " + std::string(GetSpellEffectParameterValidationErrorName(parameterValidation.error)) + "." });
            }

            if (descriptor->type == SpellEffectTypeEnum::ApplyAura)
            {
                const i32 referencedSpellValue = effect.parameters[0];
                if (referencedSpellValue <= 0 || !spellStorage->Has(static_cast<u32>(referencedSpellValue)))
                {
                    _diagnostics.push_back({ path + ".parameters.1", "ApplyAura references a missing spell." });
                }
                else if (!auraStorage->Has(static_cast<u32>(referencedSpellValue)))
                {
                    _diagnostics.push_back({ path + ".parameters.1", "ApplyAura references a spell without an aura definition." });
                }
            }
        }

        if (_draft->aura)
        {
            const SpellEditorAuraDraft& aura = *_draft->aura;
            if (!std::isfinite(aura.duration) || (aura.duration <= 0.0f && aura.duration != INFINITE_AURA_DURATION))
                _diagnostics.push_back({ "aura.duration", "Duration must be positive or -1 for infinite." });
            if (aura.stacksPerApplication == 0)
                _diagnostics.push_back({ "aura.stacksPerApplication", "Stacks per application must be at least one." });
            if (aura.maximumStacks == 0 || aura.maximumStacks < aura.stacksPerApplication)
                _diagnostics.push_back({ "aura.maximumStacks", "Maximum stacks must be at least stacks per application." });
            if (!IsEnumValue<AuraApplicationPolicyEnumMeta>(aura.applicationPolicy))
                _diagnostics.push_back({ "aura.applicationPolicy", "Application policy is not a generated enum value." });
            if (!IsEnumValue<AuraDispositionEnumMeta>(aura.disposition))
                _diagnostics.push_back({ "aura.disposition", "Disposition is not a generated enum value." });
            if (!IsEnumValue<AuraDispelTypeEnumMeta>(aura.dispelType))
                _diagnostics.push_back({ "aura.dispelType", "Dispel type is not a generated enum value." });
            constexpr u8 lifecycleMask = static_cast<u8>(AuraLifecycleFlagsEnum::PersistThroughTargetDeath) |
                static_cast<u8>(AuraLifecycleFlagsEnum::RemoveOnCasterDeath) |
                static_cast<u8>(AuraLifecycleFlagsEnum::RemoveOnCasterWorldExit);
            if ((aura.lifecycleFlags & ~lifecycleMask) != 0)
                _diagnostics.push_back({ "aura.lifecycleFlags", "Lifecycle flags contain unsupported bits." });
        }
        else if (!_draft->constraints.empty())
        {
            _diagnostics.push_back({ "constraints", "Aura constraints cannot exist without an aura definition." });
        }

        constexpr u8 validConstraintOverrideMask =
            static_cast<u8>(AuraConstraintOverrideFlagsEnum::Scope) |
            static_cast<u8>(AuraConstraintOverrideFlagsEnum::MaximumApplications) |
            static_cast<u8>(AuraConstraintOverrideFlagsEnum::OverflowBehavior);
        std::unordered_set<u32> constraintIDs;
        std::unordered_set<u32> constraintGroupIDs;
        for (const SpellEditorConstraintDraft& constraint : _draft->constraints)
        {
            const std::string path = "constraints." + std::to_string(constraint.id);
            if (constraint.id == 0 || !constraintIDs.insert(constraint.id).second)
                _diagnostics.push_back({ path, "Stable constraint ID is zero or duplicated in this draft." });
            if (!constraintGroupIDs.insert(constraint.groupID).second)
                _diagnostics.push_back({ path + ".groupID", "An aura may only have one membership in each constraint group." });
            if (constraintStorage->Has(constraint.id))
            {
                const auto& snapshotConstraint = constraintStorage->Get<MetaGen::Shared::ClientDB::SpellAuraConstraintRecord>(constraint.id);
                if (_draft->isCreate || snapshotConstraint.spellID != _draft->spellID)
                    _diagnostics.push_back({ path, "Stable constraint ID belongs to another authoritative spell." });
            }
            if (!groupStorage->Has(constraint.groupID))
                _diagnostics.push_back({ path + ".groupID", "Constraint references a missing reusable group." });
            if (!IsEnumValue<AuraConstraintScopeEnumMeta>(constraint.scope))
                _diagnostics.push_back({ path + ".scope", "Constraint scope is not a generated enum value." });
            if (constraint.maximumApplications == 0)
                _diagnostics.push_back({ path + ".maximumApplications", "Maximum applications must be at least one." });
            if (!IsEnumValue<AuraConstraintOverflowEnumMeta>(constraint.overflowBehavior))
                _diagnostics.push_back({ path + ".overflowBehavior", "Overflow behavior is not a generated enum value." });
            if ((constraint.overrideMask & ~validConstraintOverrideMask) != 0)
                _diagnostics.push_back({ path + ".overrideMask", "Constraint override mask contains unsupported bits." });
        }

        std::unordered_set<u32> procLinkIDs;
        const u64 allowedEffectMask = _draft->effects.size() == 64
            ? std::numeric_limits<u64>::max()
            : (u64{ 1 } << _draft->effects.size()) - 1;
        for (const SpellEditorProcLinkDraft& procLink : _draft->procLinks)
        {
            const std::string path = "procs.links." + std::to_string(procLink.id);
            if (procLink.id == 0 || !procLinkIDs.insert(procLink.id).second)
                _diagnostics.push_back({ path, "Stable ProcLink ID is zero or duplicated in this draft." });
            if (procLinkStorage->Has(procLink.id))
            {
                const auto& snapshotLink = procLinkStorage->Get<MetaGen::Shared::ClientDB::SpellProcLinkRecord>(procLink.id);
                if (_draft->isCreate || snapshotLink.spellID != _draft->spellID)
                    _diagnostics.push_back({ path, "Stable ProcLink ID belongs to another authoritative spell." });
            }
            if (!procDataStorage->Has(procLink.procDataID))
            {
                _diagnostics.push_back({ path + ".procDataID", "ProcLink references missing reusable ProcData." });
            }
            else
            {
                const auto& procData = procDataStorage->Get<MetaGen::Shared::ClientDB::SpellProcDataRecord>(procLink.procDataID);
                if (!IsProcDataVisibleToSpell(procData, _draft->spellID))
                    _diagnostics.push_back({ path + ".procDataID", "ProcLink references private ProcData owned by another spell." });
            }
            if (procLink.effectMask == 0)
                _diagnostics.push_back({ path + ".effects", "ProcLink must select at least one effect row." });
            if ((procLink.effectMask & ~allowedEffectMask) != 0)
                _diagnostics.push_back({ path + ".effects", "ProcLink selects an effect row that does not exist." });
        }

        return _diagnostics;
    }

    bool SpellEditorBackend::BuildPayload(const SpellEditorDraft& draft, std::shared_ptr<Bytebuffer>& payload) const
    {
        payload = Bytebuffer::BorrowRuntime(MAX_PAYLOAD_SIZE);
        if (!payload)
            return false;

        const GameDefine::Database::Spell spell = {
            .id = draft.spellID,
            .name = draft.name,
            .description = draft.description,
            .auraDescription = draft.auraDescription,
            .iconID = draft.iconID,
            .castTime = draft.castTime,
            .cooldown = draft.cooldown,
            .targetSelector = draft.targetSelector,
            .targetShape = draft.targetShape,
            .targetRelation = draft.targetRelation,
            .targetRecipientMask = draft.targetRecipientMask,
            .rangePolicy = draft.rangePolicy,
            .minimumRange = draft.minimumRange,
            .maximumRange = draft.maximumRange,
            .targetRadius = draft.targetRadius,
            .maximumTargets = draft.maximumTargets
        };

        if (!payload->PutU8(SPELL_SYNC_VERSION) || !GameDefine::Database::Spell::Write(payload.get(), spell) || !payload->PutU8(draft.aura.has_value() ? 1 : 0))
        {
            return false;
        }
        if (draft.aura)
        {
            const SpellEditorAuraDraft& aura = *draft.aura;
            const GameDefine::Database::SpellAura value = {
                draft.spellID, aura.duration, aura.stacksPerApplication, aura.maximumStacks,
                aura.applicationPolicy, aura.disposition, aura.dispelType, aura.lifecycleFlags
            };
            if (!GameDefine::Database::SpellAura::Write(payload.get(), value))
                return false;
        }

        if (!payload->PutU16(static_cast<u16>(draft.constraints.size())))
            return false;
        for (const SpellEditorConstraintDraft& constraint : draft.constraints)
        {
            const MetaGen::Shared::ClientDB::SpellAuraConstraintRecord value = {
                draft.spellID, constraint.groupID, constraint.scope,
                constraint.maximumApplications, constraint.overflowBehavior, constraint.overrideMask
            };
            if (!payload->PutU32(constraint.id) || !payload->Serialize(value))
                return false;
        }

        if (!payload->PutU16(static_cast<u16>(draft.effects.size())))
            return false;
        for (const SpellEditorEffectDraft& effect : draft.effects)
        {
            const GameDefine::Database::SpellEffect value = {
                effect.id, draft.spellID, effect.priority, effect.type, effect.parameters
            };
            if (!GameDefine::Database::SpellEffect::Write(payload.get(), value))
                return false;
        }

        if (!payload->PutU16(0))
            return false;

        if (!payload->PutU16(static_cast<u16>(draft.procLinks.size())))
            return false;
        for (const SpellEditorProcLinkDraft& procLink : draft.procLinks)
        {
            const MetaGen::Shared::ClientDB::SpellProcLinkRecord value = {
                draft.spellID, procLink.effectMask, procLink.procDataID
            };
            if (!payload->PutU32(procLink.id) || !payload->Serialize(value))
                return false;
        }

        return payload->writtenData <= MAX_PAYLOAD_SIZE;
    }

    bool SpellEditorBackend::SendDraft(const SpellEditorDraft& draft, MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum mutationType)
    {
        SpellEditorData* data = GetData();
        ECS::Singletons::NetworkState* networkState = GetNetworkState();
        if (!data || !networkState || !networkState->client || !networkState->client->IsConnected() || !networkState->isInWorld)
            return false;

        std::shared_ptr<Bytebuffer> payload;
        if (!BuildPayload(draft, payload))
            return false;

        constexpr size_t chunkCapacity = 1024;
        const u32 payloadSize = static_cast<u32>(payload->writtenData);
        const size_t chunkCount = (payloadSize + chunkCapacity - 1) / chunkCapacity;
        std::shared_ptr<Bytebuffer> buffer = Bytebuffer::BorrowRuntime(payloadSize + chunkCount * 32 + 64);
        if (!buffer)
            return false;

        const u32 requestID = data->StartMutationRequest();
        if (!ECS::Util::MessageBuilder::Cheat::BuildDatabaseEditorMutation(buffer, MetaGen::Shared::DatabaseEditor::DatabaseEditorTypeEnum::Spell, static_cast<u8>(MetaGen::Shared::Spell::SpellEditorArtifactEnum::Spell), mutationType, requestID, payload->GetDataPointer(), payloadSize))
        {
            return false;
        }

        networkState->client->Send(buffer);
        _pendingRequestID = requestID;
        _draftState = SpellEditorDraftState::Synchronizing;
        _serverDiagnostic.clear();
        return true;
    }

    bool SpellEditorBackend::SendConstraintGroupMutation(u32 groupID, std::string_view name, u8 defaultScope, u16 defaultMaximumApplications, u8 defaultOverflowBehavior, MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum mutationType)
    {
        using Artifact = MetaGen::Shared::Spell::SpellEditorArtifactEnum;
        using MutationType = MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum;

        SpellEditorData* data = GetData();
        ECS::Singletons::NetworkState* networkState = GetNetworkState();
        ::ClientDB::Data* groups = data ? data->GetStorage(Artifact::SpellAuraConstraintGroup) : nullptr;
        const bool groupExists = groups && groups->Has(groupID);
        const auto scope = static_cast<MetaGen::Shared::Spell::AuraConstraintScopeEnum>(defaultScope);
        const auto overflow = static_cast<MetaGen::Shared::Spell::AuraConstraintOverflowEnum>(defaultOverflowBehavior);
        if (groupID == 0 || name.empty() || name.size() > 64 || defaultMaximumApplications == 0 ||
            scope >= MetaGen::Shared::Spell::AuraConstraintScopeEnum::Count ||
            overflow >= MetaGen::Shared::Spell::AuraConstraintOverflowEnum::Count ||
            (mutationType != MutationType::Create && mutationType != MutationType::Update) ||
            (mutationType == MutationType::Create && groupExists) ||
            (mutationType == MutationType::Update && !groupExists) ||
            !data || data->state != SpellEditorDataState::Ready ||
            !networkState || !networkState->client || !networkState->client->IsConnected() ||
            !networkState->isInWorld || _pendingRequestID != 0 ||
            (_draftState != SpellEditorDraftState::None && _draftState != SpellEditorDraftState::Clean && _draftState != SpellEditorDraftState::Dirty))
        {
            return false;
        }

        std::shared_ptr<Bytebuffer> payload = Bytebuffer::Borrow<128>();
        if (!payload->PutU32(groupID) || !payload->PutString(name) || !payload->PutU8(defaultScope) || !payload->PutU16(defaultMaximumApplications) || !payload->PutU8(defaultOverflowBehavior))
        {
            return false;
        }

        const u32 requestID = data->StartMutationRequest();
        std::shared_ptr<Bytebuffer> buffer = Bytebuffer::Borrow<256>();
        if (!ECS::Util::MessageBuilder::Cheat::BuildDatabaseEditorMutation(buffer, MetaGen::Shared::DatabaseEditor::DatabaseEditorTypeEnum::Spell, static_cast<u8>(Artifact::SpellAuraConstraintGroup), mutationType, requestID, payload->GetDataPointer(), static_cast<u32>(payload->writtenData)))
        {
            return false;
        }

        networkState->client->Send(buffer);
        _resumeDraftStateAfterRefresh = _draftState;
        _pendingRequestID = requestID;
        _draftState = SpellEditorDraftState::Synchronizing;
        _serverDiagnostic.clear();
        return true;
    }

    bool SpellEditorBackend::SendProcDataMutation(const GameDefine::Database::SpellProcData& value, u32 ownerSpellID, std::string_view name, MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum mutationType)
    {
        using Artifact = MetaGen::Shared::Spell::SpellEditorArtifactEnum;
        using MutationType = MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum;
        using namespace MetaGen::Shared::Spell;

        SpellEditorData* data = GetData();
        ECS::Singletons::NetworkState* networkState = GetNetworkState();
        ::ClientDB::Data* procData = data ? data->GetStorage(Artifact::SpellProcData) : nullptr;
        ::ClientDB::Data* spells = data ? data->GetStorage(Artifact::Spell) : nullptr;
        const bool definitionExists = procData && procData->Has(value.id);

        const auto Reject = [this](std::string_view reason)
        {
            _serverDiagnostic = "ProcData mutation was not sent: ";
            _serverDiagnostic.append(reason);
            return false;
        };

        if (value.id == 0)
            return Reject("ID 0 is reserved.");
        if (ownerSpellID == 0 && name.empty())
            return Reject("shared ProcData requires a debug name.");
        if (name.size() > 64)
            return Reject("the debug name exceeds 64 characters.");
        if (ownerSpellID != 0 && (!spells || !spells->Has(ownerSpellID)))
            return Reject("the private owner spell does not exist.");
        if (value.phaseMask == 0)
            return Reject("the phase mask cannot be empty.");
        if (!IsSupportedMaskValue<SpellProcPhaseMaskEnumMeta>(value.phaseMask))
            return Reject("the phase mask contains unsupported bits.");
        if (value.typeMask == 0)
            return Reject("the type mask cannot be empty.");
        if (!IsSupportedMaskValue<SpellProcTypeMaskEnumMeta>(value.typeMask))
            return Reject("the type mask contains unsupported bits.");
        if (value.hitMask == 0)
            return Reject("the hit mask cannot be empty.");
        if (!IsSupportedMaskValue<SpellProcHitMaskEnumMeta>(value.hitMask))
            return Reject("the hit mask contains unsupported bits.");
        if (!IsSupportedMaskValue<SpellProcFlagEnumMeta>(value.flags))
            return Reject("the flags contain unsupported bits.");
        if (!std::isfinite(value.procsPerMinute) || value.procsPerMinute < 0.0f)
            return Reject("procs per minute must be finite and non-negative.");
        if (!std::isfinite(value.chanceToProc) || value.chanceToProc < 0.0f || value.chanceToProc > 1.0f)
            return Reject("proc chance must be between 0 and 1.");
        if (value.procsPerMinute == 0.0f && value.chanceToProc == 0.0f)
            return Reject("either procs per minute or proc chance must be greater than zero.");
        if (value.charges == 0 || value.charges < -1)
            return Reject("charges must be -1 for unlimited or greater than zero.");
        if (mutationType != MutationType::Create && mutationType != MutationType::Update)
            return Reject("the requested mutation type is invalid.");
        if (mutationType == MutationType::Create && definitionExists)
            return Reject("the allocated ProcData ID already exists.");
        if (mutationType == MutationType::Update && !definitionExists)
            return Reject("the ProcData definition no longer exists.");
        if (!data || data->state != SpellEditorDataState::Ready)
            return Reject("the authoritative editor data is not ready.");
        if (!networkState || !networkState->client || !networkState->client->IsConnected())
            return Reject("the game client is not connected.");
        if (!networkState->isInWorld)
            return Reject("the character is not in the world.");
        if (_pendingRequestID != 0)
            return Reject("another editor mutation is still pending.");
        if (_draftState != SpellEditorDraftState::None && _draftState != SpellEditorDraftState::Clean && _draftState != SpellEditorDraftState::Dirty)
        {
            return Reject("the spell draft is busy.");
        }

        if (mutationType == MutationType::Update)
        {
            const auto& existing = procData->Get<MetaGen::Shared::ClientDB::SpellProcDataRecord>(value.id);
            const bool promotingPrivateDefinition = existing.ownerSpellID != 0 && ownerSpellID == 0;
            if (existing.ownerSpellID != ownerSpellID && !promotingPrivateDefinition)
                return Reject("the ProcData owner cannot be changed.");
        }

        std::shared_ptr<Bytebuffer> payload = Bytebuffer::Borrow<192>();
        if (!payload->PutU32(value.id) || !payload->PutU32(ownerSpellID) || !payload->PutString(name) ||
            !payload->PutU32(value.phaseMask) || !payload->PutU64(value.typeMask) || !payload->PutU64(value.hitMask) ||
            !payload->PutU64(value.flags) || !payload->PutF32(value.procsPerMinute) ||
            !payload->PutF32(value.chanceToProc) || !payload->PutU32(value.internalCooldownMS) || !payload->PutI32(value.charges))
        {
            return Reject("the mutation payload could not be built.");
        }

        const u32 requestID = data->StartMutationRequest();
        std::shared_ptr<Bytebuffer> buffer = Bytebuffer::Borrow<384>();
        if (!ECS::Util::MessageBuilder::Cheat::BuildDatabaseEditorMutation(buffer, MetaGen::Shared::DatabaseEditor::DatabaseEditorTypeEnum::Spell, static_cast<u8>(Artifact::SpellProcData), mutationType, requestID, payload->GetDataPointer(), static_cast<u32>(payload->writtenData)))
        {
            return Reject("the network request could not be built.");
        }

        networkState->client->Send(buffer);
        _resumeDraftStateAfterRefresh = _draftState;
        _pendingRequestID = requestID;
        _draftState = SpellEditorDraftState::Synchronizing;
        _serverDiagnostic.clear();
        return true;
    }

    bool SpellEditorBackend::Submit()
    {
        if (!_draft || _draftState == SpellEditorDraftState::Synchronizing || _draftState == SpellEditorDraftState::Refreshing)
            return false;
        if (!Validate().empty())
            return false;

        return SendDraft(*_draft, _draft->isCreate ? MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Create : MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Update);
    }

    bool SpellEditorBackend::DeleteSpell(u32 spellID)
    {
        SpellEditorData* data = GetData();
        ECS::Singletons::NetworkState* networkState = GetNetworkState();
        if (!data || data->state != SpellEditorDataState::Ready || !networkState || !networkState->client || !networkState->client->IsConnected() || !networkState->isInWorld || _pendingRequestID != 0)
        {
            return false;
        }

        ::ClientDB::Data* spellStorage = data->GetStorage(MetaGen::Shared::Spell::SpellEditorArtifactEnum::Spell);
        if (!spellStorage->Has(spellID))
            return false;

        std::shared_ptr<Bytebuffer> payload = Bytebuffer::Borrow<sizeof(u32)>();
        if (!payload->PutU32(spellID))
            return false;

        const u32 requestID = data->StartMutationRequest();
        std::shared_ptr<Bytebuffer> buffer = Bytebuffer::Borrow<128>();
        if (!ECS::Util::MessageBuilder::Cheat::BuildDatabaseEditorMutation(buffer, MetaGen::Shared::DatabaseEditor::DatabaseEditorTypeEnum::Spell, static_cast<u8>(MetaGen::Shared::Spell::SpellEditorArtifactEnum::Spell), MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Delete, requestID, payload->GetDataPointer(), static_cast<u32>(payload->writtenData)))
        {
            return false;
        }

        networkState->client->Send(buffer);
        _pendingRequestID = requestID;
        _refreshSpellID = spellID;
        _draftState = SpellEditorDraftState::Synchronizing;
        _serverDiagnostic.clear();
        return true;
    }

    bool SpellEditorBackend::SubmitStoredSpellUpdate(u32 spellID)
    {
        SpellEditorDraft storedDraft;
        if (!LoadDraft(spellID, storedDraft))
            return false;

        return SendDraft(storedDraft, MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Update);
    }

    u32 SpellEditorBackend::AllocateID(MetaGen::Shared::Spell::SpellEditorArtifactEnum artifact, const std::vector<u32>& additionalIDs) const
    {
        SpellEditorData* data = GetData();
        ::ClientDB::Data* storage = data ? data->GetStorage(artifact) : nullptr;
        if (!storage)
            return 0;

        u32 highestID = 0;
        for (const ::ClientDB::IDListEntry& entry : storage->GetIDList())
            highestID = std::max(highestID, entry.id);
        for (u32 id : additionalIDs)
            highestID = std::max(highestID, id);
        return highestID == std::numeric_limits<u32>::max() ? 0 : highestID + 1;
    }

    bool SpellEditorBackend::ConsumeMutationSuccess()
    {
        const bool result = _mutationSuccess;
        _mutationSuccess = false;
        return result;
    }
}
