#pragma once
#include <Base/Types.h>

namespace DB::Client::Definitions
{
	struct Map;
}

namespace ECS::Util
{
	namespace MapDB
	{
		bool GetMapFromNameHash(u32 nameHash, DB::Client::Definitions::Map* map);
		bool GetMapFromName(const std::string& name, DB::Client::Definitions::Map* map);

		bool GetMapFromInternalNameHash(u32 nameHash, DB::Client::Definitions::Map* map);
		bool GetMapFromInternalName(const std::string& name, DB::Client::Definitions::Map* map);
	}
}