#pragma once

#include "Game-Lib/Editor/DatabaseEditorData.h"

#include <MetaGen/Shared/Interaction/Interaction.h>

namespace Editor
{
    class InteractionEditorData : public DatabaseEditorData
    {
    public:
        InteractionEditorData();

        ::ClientDB::Data* GetStorage(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum artifact);
        const ::ClientDB::Data* GetStorage(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum artifact) const;

    private:
        bool ValidateSnapshot(std::vector<::ClientDB::Data>& storages) const override;
        bool ApplyChangeSet(u16 changeCount, Bytebuffer& payload) override;
    };
}
