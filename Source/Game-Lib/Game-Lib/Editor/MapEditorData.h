#pragma once

#include "Game-Lib/Editor/DatabaseEditorData.h"

namespace Editor
{
    class MapEditorData : public DatabaseEditorData
    {
    public:
        MapEditorData();

        ::ClientDB::Data* GetMapStorage() { return GetStorage(0); }
        const ::ClientDB::Data* GetMapStorage() const { return GetStorage(0); }

    private:
        bool ValidateSnapshot(std::vector<::ClientDB::Data>& storages) const override;
    };
}
