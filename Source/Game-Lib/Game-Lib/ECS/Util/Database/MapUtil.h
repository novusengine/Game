#pragma once
#include <Base/Types.h>

namespace Generated
{
    struct MapRecord;
}

namespace ECSUtil::Map
{
    bool Refresh();

    bool GetMapFromInternalNameHash(u32 nameHash, Generated::MapRecord* map);
    bool GetMapFromInternalName(const std::string& name, Generated::MapRecord* map);

    u32 GetMapIDFromInternalName(const std::string& internalName);

    bool AddMap(const std::string& internalName, const std::string& name, Generated::MapRecord& map);
    bool RemoveMap(u32 mapID);

    bool SetMapInternalName(const std::string& internalName, const std::string& name);
    bool SetMapInternalName(u32 mapID, const std::string& name);

    void MarkDirty();
}