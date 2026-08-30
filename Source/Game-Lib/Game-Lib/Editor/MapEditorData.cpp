#include "MapEditorData.h"

#include <MetaGen/Shared/ClientDB/ClientDB.h>

namespace Editor
{
    MapEditorData::MapEditorData()
        : DatabaseEditorData(1)
    {
    }

    bool MapEditorData::ValidateSnapshot(std::vector<::ClientDB::Data>& storages) const
    {
        if (storages.size() != 1)
            return false;

        const auto& fields = storages.front().GetFields();
        const auto& expectedFields = MetaGen::Shared::ClientDB::MapRecord::FIELD_LIST;
        if (fields.size() != expectedFields.size())
            return false;

        for (size_t index = 0; index < fields.size(); ++index)
        {
            if (fields[index].name != expectedFields[index].name || fields[index].type != expectedFields[index].type || fields[index].count != expectedFields[index].count)
            {
                return false;
            }
        }

        return true;
    }
}
