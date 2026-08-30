#include "SpellEditorData.h"

#include "Game-Lib/ECS/Util/Database/SpellUtil.h"

#include <MetaGen/Shared/ClientDB/ClientDB.h>

namespace Editor
{
    namespace
    {
        bool HasExpectedSchema(::ClientDB::Data& storage, const std::vector<::ClientDB::FieldInfo>& expectedFields)
        {
            const std::vector<::ClientDB::FieldInfo>& fields = storage.GetFields();
            if (fields.size() != expectedFields.size())
                return false;

            for (size_t index = 0; index < fields.size(); ++index)
            {
                const ::ClientDB::FieldInfo& field = fields[index];
                const ::ClientDB::FieldInfo& expectedField = expectedFields[index];
                if (field.name != expectedField.name || field.type != expectedField.type || field.count != expectedField.count)
                    return false;
            }

            return true;
        }
    }

    SpellEditorData::SpellEditorData()
        : DatabaseEditorData(static_cast<u8>(MetaGen::Shared::Spell::SpellEditorArtifactEnum::Count))
    {
    }

    bool SpellEditorData::ValidateSnapshot(std::vector<::ClientDB::Data>& storages) const
    {
        using Artifact = MetaGen::Shared::Spell::SpellEditorArtifactEnum;
        return HasExpectedSchema(storages[static_cast<size_t>(Artifact::Spell)], MetaGen::Shared::ClientDB::SpellRecord::FIELD_LIST) &&
               HasExpectedSchema(storages[static_cast<size_t>(Artifact::SpellAura)], MetaGen::Shared::ClientDB::SpellAuraRecord::FIELD_LIST) &&
               HasExpectedSchema(storages[static_cast<size_t>(Artifact::SpellEffects)], MetaGen::Shared::ClientDB::SpellEffectsRecord::FIELD_LIST) &&
               HasExpectedSchema(storages[static_cast<size_t>(Artifact::SpellProcData)], MetaGen::Shared::ClientDB::SpellProcDataRecord::FIELD_LIST) &&
               HasExpectedSchema(storages[static_cast<size_t>(Artifact::SpellProcLink)], MetaGen::Shared::ClientDB::SpellProcLinkRecord::FIELD_LIST) &&
               HasExpectedSchema(storages[static_cast<size_t>(Artifact::SpellAuraConstraintGroup)], MetaGen::Shared::ClientDB::SpellAuraConstraintGroupRecord::FIELD_LIST) &&
               HasExpectedSchema(storages[static_cast<size_t>(Artifact::SpellAuraConstraint)], MetaGen::Shared::ClientDB::SpellAuraConstraintRecord::FIELD_LIST);
    }

    void SpellEditorData::OnSnapshotLoaded()
    {
        using Artifact = MetaGen::Shared::Spell::SpellEditorArtifactEnum;
        spellIndex.spellIDToEffectList.clear();
        ::ClientDB::Data* effects = GetStorage(Artifact::SpellEffects);
        effects->Each([&](u32 effectID, const MetaGen::Shared::ClientDB::SpellEffectsRecord& effect)
        {
            ECSUtil::Spell::AddSpellEffect(spellIndex, effect.spellID, effectID);
            return true;
        });

        for (const auto& entry : spellIndex.spellIDToEffectList)
        {
            ECSUtil::Spell::SortSpellEffects(spellIndex, effects, entry.first);
        }
    }

    ::ClientDB::Data* SpellEditorData::GetStorage(MetaGen::Shared::Spell::SpellEditorArtifactEnum type)
    {
        return DatabaseEditorData::GetStorage(static_cast<u8>(type));
    }

    const ::ClientDB::Data* SpellEditorData::GetStorage(MetaGen::Shared::Spell::SpellEditorArtifactEnum type) const
    {
        return DatabaseEditorData::GetStorage(static_cast<u8>(type));
    }
}
