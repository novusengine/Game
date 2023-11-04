#pragma once
#include "ClientDBCollection.h"
#include "Game/Util/ServiceLocator.h"

#include <Base/Types.h>
#include <Base/Container/StringTable.h>

#include <FileFormat/Novus/ClientDB/ClientDB.h>
#include <FileFormat/Novus/ClientDB/Definitions.h>

#include <entt/entt.hpp>
#include <robinhood/robinhood.h>

namespace ECS::Singletons
{
	struct MapDB
	{
	public:
		MapDB() { }

        bool Refresh()
        {
            entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
            entt::registry::context& ctx = registry->ctx();

            ClientDBCollection& clientDBCollection = ctx.get<ClientDBCollection>();
            auto maps = clientDBCollection.Get<ClientDB::Definitions::Map>(ClientDBHash::Map);

            mapNames.clear();
            mapInternalNames.clear();
            mapNameHashToID.clear();
            mapInternalNameHashToID.clear();

            u32 numRecords = maps.Count();
            mapNames.reserve(numRecords);
            mapInternalNames.reserve(numRecords);
            mapNameHashToID.reserve(numRecords);
            mapInternalNameHashToID.reserve(numRecords);

            for (const ClientDB::Definitions::Map& map : maps)
            {
                if (!maps.IsValid(map))
                    continue;

                if (map.name == std::numeric_limits<u32>().max())
                    continue;

                const std::string& mapName = maps.GetString(map.name);
                u32 mapNameHash = StringUtils::fnv1a_32(mapName.c_str(), mapName.length());

                const std::string& mapInternalName = maps.GetString(map.internalName);
                u32 mapInternalNameHash = StringUtils::fnv1a_32(mapInternalName.c_str(), mapInternalName.length());

               mapNames.push_back(mapName);
               mapInternalNames.push_back(mapInternalName);
               mapNameHashToID[mapNameHash] = map.id;
               mapInternalNameHashToID[mapInternalNameHash] = map.id;
            }

            return true;
        }

	public:
		std::vector<std::string> mapNames;
		std::vector<std::string> mapInternalNames;
		robin_hood::unordered_map<u32, u32> mapNameHashToID;
		robin_hood::unordered_map<u32, u32> mapInternalNameHashToID;
	};
}