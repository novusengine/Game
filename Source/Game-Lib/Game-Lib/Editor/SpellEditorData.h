#pragma once

#include "Game-Lib/ECS/Singletons/Database/SpellSingleton.h"
#include "Game-Lib/Editor/DatabaseEditorData.h"

#include <MetaGen/Shared/Spell/Spell.h>

namespace Editor
{
    using SpellEditorDataState = DatabaseEditorDataState;
    using SpellEditorMutationResult = DatabaseEditorMutationResult;

    class SpellEditorData : public DatabaseEditorData
    {
    public:
        SpellEditorData();

        ::ClientDB::Data* GetStorage(MetaGen::Shared::Spell::SpellEditorArtifactEnum type);
        const ::ClientDB::Data* GetStorage(MetaGen::Shared::Spell::SpellEditorArtifactEnum type) const;

    public:
        ECS::Singletons::SpellSingleton spellIndex;

    private:
        bool ValidateSnapshot(std::vector<::ClientDB::Data>& storages) const override;
        void OnSnapshotLoaded() override;
    };
}
