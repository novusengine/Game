#pragma once
#include <Base/Types.h>

namespace ClientDB::Definitions
{
    struct Map;
}

namespace ECS::Util
{
    namespace Map
    {
        bool Refresh();
        bool GetMapFromInternalNameHash(u32 nameHash, ClientDB::Definitions::Map* map);
        bool GetMapFromInternalName(const std::string& name, ClientDB::Definitions::Map* map);

        u32 GetMapIDFromInternalName(const std::string& internalName);

        bool AddMap(ClientDB::Definitions::Map& map);
        bool RemoveMap(u32 mapID);

        bool SetMapInternalName(const std::string& internalName, const std::string& name);
        bool SetMapInternalName(u32 mapID, const std::string& name);

        void MarkDirty();
    }
}