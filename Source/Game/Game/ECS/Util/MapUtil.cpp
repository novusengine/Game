#include "MapUtil.h"
#include "Game/Application/EnttRegistries.h"
#include "Game/ECS/Singletons/ClientDBCollection.h"
#include "Game/ECS/Singletons/MapDB.h"
#include "Game/Util/ServiceLocator.h"

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
            auto maps = clientDBCollection.Get<Definitions::Map>(ClientDBHash::Map);

            mapDB.mapInternalNameHashToID.clear();

            u32 numRecords = maps.Count();
            mapDB.mapInternalNameHashToID.reserve(numRecords);

            for (const ClientDB::Definitions::Map& map : maps)
            {
                if (!maps.IsValid(map))
                    continue;

                if (!maps.HasString(map.internalName))
                    continue;

                const std::string& mapInternalName = maps.GetString(map.internalName);
                u32 mapInternalNameHash = StringUtils::fnv1a_32(mapInternalName.c_str(), mapInternalName.length());
                mapDB.mapInternalNameHashToID[mapInternalNameHash] = map.GetID();
            }

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

        bool AddMap(Definitions::Map& map)
        {
            entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;

            auto& clientDBCollection = registry->ctx().get<ClientDBCollection>();
            auto mapStorage = clientDBCollection.Get<Definitions::Map>(ClientDBHash::Map);

            if (!mapStorage.HasString(map.name))
                return false;

            if (!mapStorage.HasString(map.internalName))
                return false;

            mapStorage.Add(map);

            if (!mapStorage.IsValid(map))
                return false;

            u32 mapID = map.GetID();
            const std::string& mapInternalName = mapStorage.GetString(map.internalName);
            u32 mapInternalNameHash = StringUtils::fnv1a_32(mapInternalName.c_str(), mapInternalName.length());

            auto& mapDB = registry->ctx().get<MapDB>();
            mapDB.mapInternalNameHashToID[mapID] = mapInternalNameHash;
            return true;
        }

        bool RemoveMap(u32 mapID)
        {
            entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;

            auto& clientDBCollection = registry->ctx().get<ClientDBCollection>();
            auto mapStorage = clientDBCollection.Get<Definitions::Map>(ClientDBHash::Map);

            if (!mapStorage.Contains(mapID))
                return false;

            const Definitions::Map& map = mapStorage.GetByID(mapID);
            if (mapStorage.Contains(map.internalName))
            {
                auto& mapDB = registry->ctx().get<MapDB>();

                const std::string& internalName = mapStorage.GetString(map.internalName);
                u32 internalNameHash = StringUtils::fnv1a_32(internalName.c_str(), internalName.length());

                mapDB.mapInternalNameHashToID.erase(internalNameHash);
            }

            mapStorage.Remove(mapID);

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

            auto& clientDBCollection = ctx.get<ClientDBCollection>();
            auto mapStorage = clientDBCollection.Get<Definitions::Map>(ClientDBHash::Map);

            if (!mapStorage.Contains(mapID))
                return false;

            Definitions::Map& map = mapStorage.GetByID(mapID);
            map.name = mapStorage.AddString(name);

            auto& mapDB = ctx.get<MapDB>();

            u32 nameHash = StringUtils::fnv1a_32(name.c_str(), name.length());
            mapDB.mapInternalNameHashToID[mapID] = nameHash;

            return true;
        }

        void MarkDirty()
        {
            entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
            entt::registry::context& ctx = registry->ctx();

            auto& clientDBCollection = ctx.get<ClientDBCollection>();
            auto mapStorage = clientDBCollection.Get<Definitions::Map>(ClientDBHash::Map);

            mapStorage.MarkDirty();
        }
	}
}