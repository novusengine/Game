#include "MapDBUtil.h"
#include "Game/Application/EnttRegistries.h"
#include "Game/ECS/Singletons/MapDB.h"
#include "Game/Util/ServiceLocator.h"

#include <Base/Util/StringUtils.h>

#include <FileFormat/Novus/ClientDB/ClientDB.h>
#include <FileFormat/Novus/ClientDB/Definitions.h>

#include <entt/entt.hpp>

namespace ECS::Util
{
	namespace MapDB
	{
		bool GetMapFromNameHash(u32 nameHash, DB::Client::Definitions::Map* map)
		{
			entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
			entt::registry::context& ctx = registry->ctx();

			auto& mapDB = ctx.get<Singletons::MapDB>();

			auto itr = mapDB.mapNameHashToID.find(nameHash);
			if (itr == mapDB.mapNameHashToID.end())
				return false;

			return true;
		}

		bool GetMapFromName(const std::string& name, DB::Client::Definitions::Map* map)
		{
			u32 nameHash = StringUtils::fnv1a_32(name.c_str(), name.length());
			return GetMapFromNameHash(nameHash, map);
		}

		bool GetMapFromInternalNameHash(u32 nameHash, DB::Client::Definitions::Map* map)
		{
			entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
			entt::registry::context& ctx = registry->ctx();

			auto& mapDB = ctx.get<Singletons::MapDB>();

			auto itr = mapDB.mapInternalNameHashToID.find(nameHash);
			if (itr == mapDB.mapInternalNameHashToID.end())
				return false;

			return true;
		}
		bool GetMapFromInternalName(const std::string& name, DB::Client::Definitions::Map* map)
		{
			u32 nameHash = StringUtils::fnv1a_32(name.c_str(), name.length());
			return GetMapFromNameHash(nameHash, map);
		}
	}
}