#pragma once
#include <Base/Types.h>
#include <Base/Container/StringTable.h>

#include <FileFormat/Novus/ClientDB/ClientDB.h>
#include <FileFormat/Novus/ClientDB/Definitions.h>

#include <robinhood/robinhood.h>

namespace ECS::Singletons
{
	struct MapDB
	{
	public:
		MapDB() {}

		DB::Client::ClientDB<DB::Client::Definitions::Map> entries;

		std::vector<std::string> mapNames;
		std::vector<std::string> mapInternalNames;
		robin_hood::unordered_map<u32, u32> mapNameHashToIndex;
		robin_hood::unordered_map<u32, u32> mapInternalNameHashToIndex;
	};
}