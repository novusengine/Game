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
	namespace MapDBUtil
	{
		DB::Client::Definitions::Map* GetMapFromNameHash(u32 nameHash)
		{
			entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
			entt::registry::context& ctx = registry->ctx();

			auto& mapDB = ctx.at<Singletons::MapDB>();

			auto itr = mapDB.mapNameHashToEntryID.find(nameHash);
			if (itr == mapDB.mapNameHashToEntryID.end())
				return nullptr;

			u32 mapIndex = itr->second;
			if (mapIndex >= mapDB.entries.data.size())
				return nullptr;

			return &mapDB.entries.data[mapIndex];
		}

		DB::Client::Definitions::Map* GetMapFromName(const std::string& name)
		{
			u32 nameHash = StringUtils::fnv1a_32(name.c_str(), name.length());
			return GetMapFromNameHash(nameHash);
		}
	}
}