#pragma once

#include "Game-Lib/Editor/SpellEditorData.h"

#include <Gameplay/GameDefine.h>

#include <MetaGen/Shared/ClientDB/ClientDB.h>
#include <MetaGen/Shared/DatabaseEditor/DatabaseEditor.h>
#include <MetaGen/Shared/Spell/Spell.h>

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class Bytebuffer;

namespace Editor
{
    enum class SpellEditorDraftState : u8
    {
        None,
        Clean,
        Dirty,
        Synchronizing,
        MutationFailed,
        Refreshing
    };

    struct SpellEditorDiagnostic
    {
    public:
        std::string path;
        std::string message;
    };

    struct SpellEditorEffectDraft
    {
    public:
        u32 id = 0;
        u8 priority = 0;
        u8 type = 0;
        std::array<i32, 6> parameters = {};
    };

    struct SpellEditorAuraDraft
    {
    public:
        f32 duration = -1.0f;
        u16 stacksPerApplication = 1;
        u16 maximumStacks = 1;
        u8 applicationPolicy = 0;
        u8 disposition = 0;
        u8 dispelType = 0;
        u8 lifecycleFlags = 0;
    };

    struct SpellEditorConstraintDraft
    {
    public:
        u32 id = 0;
        u32 groupID = 0;
        u8 scope = 0;
        u16 maximumApplications = 1;
        u8 overflowBehavior = 0;
        u8 overrideMask = 0;
    };

    struct SpellEditorProcLinkDraft
    {
    public:
        u32 id = 0;
        u32 procDataID = 0;
        u64 effectMask = 0;
    };

    struct SpellEditorDraft
    {
    public:
        u32 spellID = 0;
        bool isCreate = false;
        std::string name;
        std::string description;
        std::string auraDescription;
        u32 iconID = 0;
        f32 castTime = 0.0f;
        f32 cooldown = 0.0f;
        u8 targetSelector = 0;
        u8 targetShape = 0;
        u8 targetRelation = 0;
        u8 targetRecipientMask = 0;
        u8 rangePolicy = 0;
        f32 minimumRange = 0.0f;
        f32 maximumRange = 0.0f;
        f32 targetRadius = 0.0f;
        u16 maximumTargets = 1;
        std::optional<SpellEditorAuraDraft> aura;
        std::vector<SpellEditorEffectDraft> effects;
        std::vector<SpellEditorConstraintDraft> constraints;
        std::vector<SpellEditorProcLinkDraft> procLinks;
    };

    class SpellEditorBackend
    {
    public:
        bool RequestSnapshot();
        void Update();

        bool OpenDraft(u32 spellID);
        bool CreateDraft();
        bool DuplicateDraft(u32 spellID);
        void DiscardDraft();

        bool SetSpellString(std::string_view field, std::string value);
        bool SetSpellNumber(std::string_view field, f64 value);
        bool SetAuraEnabled(bool enabled);
        bool SetAuraNumber(std::string_view field, f64 value);

        u32 AddEffect(u8 type);
        bool RemoveEffect(u32 effectID);
        bool MoveEffect(u32 effectID, i32 offset);
        bool SetEffectNumber(u32 effectID, std::string_view field, i64 value);
        bool SetEffectParameter(u32 effectID, u8 parameterIndex, i32 value);

        u32 AddConstraint(u32 groupID);
        bool RemoveConstraint(u32 constraintID);
        bool SetConstraintNumber(u32 constraintID, std::string_view field, u64 value);
        bool ResetConstraintField(u32 constraintID, std::string_view field);
        u32 CreateConstraintGroup(std::string name, u8 defaultScope, u16 defaultMaximumApplications, u8 defaultOverflowBehavior);
        bool UpdateConstraintGroup(u32 groupID, std::string name, u8 defaultScope, u16 defaultMaximumApplications, u8 defaultOverflowBehavior);
        bool DeleteConstraintGroup(u32 groupID);

        u32 CreateProcData(u32 ownerSpellID, std::string name, u32 phaseMask, u64 typeMask, u64 hitMask, u64 flags, f32 procsPerMinute, f32 chanceToProc, u32 internalCooldownMS, i32 charges);
        bool UpdateProcData(u32 procDataID, u32 ownerSpellID, std::string name, u32 phaseMask, u64 typeMask, u64 hitMask, u64 flags, f32 procsPerMinute, f32 chanceToProc, u32 internalCooldownMS, i32 charges);
        bool DeleteProcData(u32 procDataID);
        u32 AddProcLink(u32 procDataID);
        bool RemoveProcLink(u32 procLinkID);
        bool SetProcLinkProcData(u32 procLinkID, u32 procDataID);
        bool SetProcLinkEffectSelected(u32 procLinkID, u32 effectID, bool selected);

        const std::vector<SpellEditorDiagnostic>& Validate();
        bool Submit();
        bool DeleteSpell(u32 spellID);

        bool SubmitStoredSpellUpdate(u32 spellID);

        const SpellEditorDraft* GetDraft() const { return _draft ? &*_draft : nullptr; }
        SpellEditorDraftState GetDraftState() const { return _draftState; }
        u32 GetPendingRequestID() const { return _pendingRequestID; }
        const std::string& GetServerDiagnostic() const { return _serverDiagnostic; }
        bool ConsumeMutationSuccess();

    private:
        SpellEditorData* GetData() const;
        bool LoadDraft(u32 spellID, SpellEditorDraft& draft) const;
        bool SendDraft(const SpellEditorDraft& draft, MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum mutationType);
        bool SendConstraintGroupMutation(u32 groupID, std::string_view name, u8 defaultScope, u16 defaultMaximumApplications, u8 defaultOverflowBehavior, MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum mutationType);
        bool SendProcDataMutation(const GameDefine::Database::SpellProcData& value, u32 ownerSpellID, std::string_view name, MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum mutationType);
        bool BuildPayload(const SpellEditorDraft& draft, std::shared_ptr<Bytebuffer>& payload) const;
        u32 AllocateID(MetaGen::Shared::Spell::SpellEditorArtifactEnum artifact, const std::vector<u32>& additionalIDs = {}) const;
        bool CanEdit() const;
        void MarkDirty();

    private:
        std::optional<SpellEditorDraft> _draft;
        SpellEditorDraftState _draftState = SpellEditorDraftState::None;
        std::vector<SpellEditorDiagnostic> _diagnostics;
        u32 _pendingRequestID = 0;
        u32 _refreshSpellID = 0;
        bool _refreshDeletedSpell = false;
        std::optional<SpellEditorDraftState> _resumeDraftStateAfterRefresh;
        bool _mutationSuccess = false;
        std::string _serverDiagnostic;
    };
}
