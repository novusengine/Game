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
		const DB::Client::Definitions::Map* GetMapFromNameHash(u32 nameHash);
		const DB::Client::Definitions::Map* GetMapFromName(const std::string& name);
	}
}