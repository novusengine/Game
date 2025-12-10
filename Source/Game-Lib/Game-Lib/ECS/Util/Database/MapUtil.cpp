#include "MapUtil.h"
#include "Game-Lib/Application/EnttRegistries.h"
#include "Game-Lib/ECS/Singletons/Database/ClientDBSingleton.h"
#include "Game-Lib/ECS/Singletons/Database/MapSingleton.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Base/Util/StringUtils.h>

#include <FileFormat/Novus/ClientDB/ClientDB.h>

#include <MetaGen/Shared/ClientDB/ClientDB.h>

#include <entt/entt.hpp>

using namespace ClientDB;
using namespace ECS::Singletons;

namespace ECSUtil::Map
{
    bool Refresh()
    {
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->dbRegistry;
        entt::registry::context& ctx = registry->ctx();

        auto& clientDBSingleton = ctx.get<ClientDBSingleton>();
        if (!ctx.find<MapSingleton>())
            ctx.emplace<MapSingleton>();

        auto& mapSingleton = ctx.get<MapSingleton>();
        auto* mapStorage = clientDBSingleton.Get(ClientDBHash::Map);

        u32 numRecords = mapStorage->GetNumRows();
        mapSingleton.mapInternalNameHashToID.clear();
        mapSingleton.mapInternalNameHashToID.reserve(numRecords);

        mapStorage->Each([&mapSingleton, &mapStorage](u32 id, const MetaGen::Shared::ClientDB::MapRecord& map) -> bool
        {
            const std::string& mapInternalName = mapStorage->GetString(map.nameInternal);
            u32 nameHash = StringUtils::fnv1a_32(mapInternalName.c_str(), mapInternalName.length());

            mapSingleton.mapInternalNameHashToID[nameHash] = id;
            return true;
        });

        return true;
    }

    bool GetMapFromInternalNameHash(u32 nameHash, MetaGen::Shared::ClientDB::MapRecord* map)
    {
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->dbRegistry;
        entt::registry::context& ctx = registry->ctx();

        auto& mapSingleton = ctx.get<MapSingleton>();

        if (!mapSingleton.mapInternalNameHashToID.contains(nameHash))
            return false;

        return true;
    }
    bool GetMapFromInternalName(const std::string& name, MetaGen::Shared::ClientDB::MapRecord* map)
    {
        u32 nameHash = StringUtils::fnv1a_32(name.c_str(), name.length());
        return GetMapFromInternalNameHash(nameHash, map);
    }

    u32 GetMapIDFromInternalName(const std::string& internalName)
    {
        u32 result = std::numeric_limits<u32>().max();

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->dbRegistry;
        entt::registry::context& ctx = registry->ctx();
        auto& mapSingleton = ctx.get<MapSingleton>();

        u32 nameHash = StringUtils::fnv1a_32(internalName.c_str(), internalName.length());
        if (!mapSingleton.mapInternalNameHashToID.contains(nameHash))
            return result;

        result = mapSingleton.mapInternalNameHashToID[nameHash];
        return result;
    }

    bool AddMap(const std::string& internalName, const std::string& name, MetaGen::Shared::ClientDB::MapRecord& map)
    {
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->dbRegistry;
        auto& clientDBSingleton = registry->ctx().get<ClientDBSingleton>();
        auto& mapSingleton = registry->ctx().get<MapSingleton>();

        u32 mapInternalNameHash = StringUtils::fnv1a_32(internalName.c_str(), internalName.length());
        if (mapSingleton.mapInternalNameHashToID.contains(mapInternalNameHash))
            return false;

        auto* mapStorage = clientDBSingleton.Get(ClientDBHash::Map);

        map.nameInternal = mapStorage->AddString(internalName);
        map.name = mapStorage->AddString(name);
        u32 mapID = mapStorage->Add(map);

        mapSingleton.mapInternalNameHashToID[mapInternalNameHash] = mapID;
        return true;
    }

    bool RemoveMap(u32 mapID)
    {
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->dbRegistry;

        auto& clientDBSingleton = registry->ctx().get<ClientDBSingleton>();
        auto* mapStorage = clientDBSingleton.Get(ClientDBHash::Map);

        if (!mapStorage->Has(mapID))
            return false;

        const auto& map = mapStorage->Get<MetaGen::Shared::ClientDB::MapRecord>(mapID);

        const std::string& mapInternalName = mapStorage->GetString(map.name);
        u32 internalNameHash = StringUtils::fnv1a_32(mapInternalName.c_str(), mapInternalName.length());

        auto& mapSingleton = registry->ctx().get<MapSingleton>();
        mapSingleton.mapInternalNameHashToID.erase(internalNameHash);

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
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->dbRegistry;
        entt::registry::context& ctx = registry->ctx();

        auto& mapSingleton = ctx.get<MapSingleton>();
        u32 internalNameHash = StringUtils::fnv1a_32(name.c_str(), name.length());

        if (mapSingleton.mapInternalNameHashToID.contains(internalNameHash))
            return false;

        auto& clientDBSingleton = registry->ctx().get<ClientDBSingleton>();
        auto* mapStorage = clientDBSingleton.Get(ClientDBHash::Map);

        if (!mapStorage->Has(mapID))
            return false;

        auto& map = mapStorage->Get<MetaGen::Shared::ClientDB::MapRecord>(mapID);
        const std::string& previousInternalName = mapStorage->GetString(map.nameInternal);
        u32 previousInternalNameHash = StringUtils::fnv1a_32(name.c_str(), name.length());

        map.name = mapStorage->AddString(name);

        mapSingleton.mapInternalNameHashToID.erase(previousInternalNameHash);
        mapSingleton.mapInternalNameHashToID[internalNameHash] = mapID;

        return true;
    }

    void MarkDirty()
    {
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->dbRegistry;
        entt::registry::context& ctx = registry->ctx();

        auto& clientDBSingleton = registry->ctx().get<ClientDBSingleton>();
        auto* mapStorage = clientDBSingleton.Get(ClientDBHash::Map);

        mapStorage->MarkDirty();
    }
}