#pragma once

#include "Game-Lib/Editor/MapEditorData.h"

#include <Gameplay/GameDefine.h>

#include <optional>
#include <string_view>

namespace Editor
{
    class MapEditorBackend
    {
    public:
        bool RequestSnapshot();
        u32 CreateMap(u32 flags, std::string_view internalName, std::string_view name, u8 type, u16 maxPlayers);
        u32 UpdateMap(const GameDefine::Database::Map& map);
        std::optional<DatabaseEditorMutationResult> TakeMutationResult(u32 requestID);

    private:
        MapEditorData* GetData(bool create) const;
        u32 SendMutation(MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum mutationType, const u8* bytes, u32 size);
    };
}
