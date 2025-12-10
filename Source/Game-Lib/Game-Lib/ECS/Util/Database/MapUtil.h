#pragma once
#include <Base/Types.h>

namespace MetaGen::Shared::ClientDB
{
    struct MapRecord;
}

namespace ECSUtil::Map
{
    bool Refresh();

    bool GetMapFromInternalNameHash(u32 nameHash, MetaGen::Shared::ClientDB::MapRecord* map);
    bool GetMapFromInternalName(const std::string& name, MetaGen::Shared::ClientDB::MapRecord* map);

    u32 GetMapIDFromInternalName(const std::string& internalName);

    bool AddMap(const std::string& internalName, const std::string& name, MetaGen::Shared::ClientDB::MapRecord& map);
    bool RemoveMap(u32 mapID);

    bool SetMapInternalName(const std::string& internalName, const std::string& name);
    bool SetMapInternalName(u32 mapID, const std::string& name);

    void MarkDirty();
}