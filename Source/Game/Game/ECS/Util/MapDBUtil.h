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
		DB::Client::Definitions::Map* GetMapFromNameHash(u32 nameHash);
		DB::Client::Definitions::Map* GetMapFromName(const std::string& name);
	}
}