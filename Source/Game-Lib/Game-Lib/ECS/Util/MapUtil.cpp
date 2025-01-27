#include "MapUtil.h"
#include "Game-Lib/Application/EnttRegistries.h"
#include "Game-Lib/ECS/Singletons/ClientDBCollection.h"
#include "Game-Lib/ECS/Singletons/MapDB.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Base/Util/StringUtils.h>

#include <FileFormat/Novus/ClientDB/ClientDB.h>
#include <FileFormat/Novus/ClientDB/Definitions.h>

#include <entt/entt.hpp>

using namespace ClientDB;
using namespace ECS::Singletons;

namespace ECS::Util
{
    namespace Map
    {
        bool Refresh()
        {
            entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
            entt::registry::context& ctx = registry->ctx();

            auto& clientDBCollection = ctx.get<ClientDBCollection>();
            auto& mapDB = ctx.get<MapDB>();
            auto* mapStorage = clientDBCollection.Get(ClientDBHash::Map);

            mapDB.mapInternalNameHashToID.clear();

            u32 numRecords = mapStorage->GetNumRows();
            mapDB.mapInternalNameHashToID.reserve(numRecords);

            mapStorage->Each([&mapDB, &mapStorage](u32 id, const ClientDB::Definitions::Map& map) -> bool
            {
                const std::string& mapInternalName = mapStorage->GetString(map.internalName);
                u32 nameHash = StringUtils::fnv1a_32(mapInternalName.c_str(), mapInternalName.length());

                mapDB.mapInternalNameHashToID[nameHash] = id;
                return true;
            });

            return true;
        }

        bool GetMapFromInternalNameHash(u32 nameHash, ClientDB::Definitions::Map* map)
        {
            entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
            entt::registry::context& ctx = registry->ctx();

            auto& mapDB = ctx.get<MapDB>();

            if (!mapDB.mapInternalNameHashToID.contains(nameHash))
                return false;

            return true;
        }
        bool GetMapFromInternalName(const std::string& name, ClientDB::Definitions::Map* map)
        {
            u32 nameHash = StringUtils::fnv1a_32(name.c_str(), name.length());
            return GetMapFromInternalNameHash(nameHash, map);
        }

        u32 GetMapIDFromInternalName(const std::string& internalName)
        {
            u32 result = std::numeric_limits<u32>().max();

            entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
            entt::registry::context& ctx = registry->ctx();
            auto& mapDB = ctx.get<MapDB>();

            u32 nameHash = StringUtils::fnv1a_32(internalName.c_str(), internalName.length());
            if (!mapDB.mapInternalNameHashToID.contains(nameHash))
                return result;

            result = mapDB.mapInternalNameHashToID[nameHash];
            return result;
        }

        bool AddMap(const std::string& internalName, const std::string& name, Definitions::Map& map)
        {
            entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
            auto& clientDBCollection = registry->ctx().get<ClientDBCollection>();
            auto& mapDB = registry->ctx().get<MapDB>();

            u32 mapInternalNameHash = StringUtils::fnv1a_32(internalName.c_str(), internalName.length());
            if (mapDB.mapInternalNameHashToID.contains(mapInternalNameHash))
                return false;

            auto* mapStorage = clientDBCollection.Get(ClientDBHash::Map);

            map.internalName = mapStorage->AddString(internalName);
            map.name = mapStorage->AddString(name);
            u32 mapID = mapStorage->Add(map);

            mapDB.mapInternalNameHashToID[mapInternalNameHash] = mapID;
            return true;
        }

        bool RemoveMap(u32 mapID)
        {
            entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;

            auto& clientDBCollection = registry->ctx().get<ClientDBCollection>();
            auto* mapStorage = clientDBCollection.Get(ClientDBHash::Map);

            if (!mapStorage->Has(mapID))
                return false;

            const auto& map = mapStorage->Get<Definitions::Map>(mapID);

            const std::string& mapInternalName = mapStorage->GetString(map.name);
            u32 internalNameHash = StringUtils::fnv1a_32(mapInternalName.c_str(), mapInternalName.length());

            auto& mapDB = registry->ctx().get<MapDB>();
            mapDB.mapInternalNameHashToID.erase(internalNameHash);

            mapStorage->Remove(mapID);

            return true;
        }

        bool SetMapInternalName(const std::string& internalName, const std::string& name)
        {
            u32 mapID = GetMapIDFromInternalName(internalName);
            return SetMapInternalName(mapID, name);
        }
        bool SetMapInternalName(u32 mapID, const std::string& name)
        {
            entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
            entt::registry::context& ctx = registry->ctx();

            auto& mapDB = ctx.get<MapDB>();
            u32 internalNameHash = StringUtils::fnv1a_32(name.c_str(), name.length());

            if (mapDB.mapInternalNameHashToID.contains(internalNameHash))
                return false;

            auto& clientDBCollection = ctx.get<ClientDBCollection>();
            auto* mapStorage = clientDBCollection.Get(ClientDBHash::Map);

            if (!mapStorage->Has(mapID))
                return false;

            auto& map = mapStorage->Get<Definitions::Map>(mapID);
            const std::string& previousInternalName = mapStorage->GetString(map.internalName);
            u32 previousInternalNameHash = StringUtils::fnv1a_32(name.c_str(), name.length());

            map.name = mapStorage->AddString(name);

            mapDB.mapInternalNameHashToID.erase(previousInternalNameHash);
            mapDB.mapInternalNameHashToID[internalNameHash] = mapID;

            return true;
        }

        void MarkDirty()
        {
            entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
            entt::registry::context& ctx = registry->ctx();

            auto& clientDBCollection = ctx.get<ClientDBCollection>();
            auto* mapStorage = clientDBCollection.Get(ClientDBHash::Map);

            mapStorage->MarkDirty();
        }
    }
}