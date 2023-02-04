#pragma once
#include <Base/Types.h>
#include <Base/Container/StringTable.h>

#include <FileFormat/Novus/ClientDB/ClientDB.h>
#include <FileFormat/Novus/ClientDB/Definitions.h>

namespace ECS::Singletons
{
	struct ClientDBSingleton
	{
		ClientDBSingleton() {}

		DB::Client::ClientDB<DB::Client::Definitions::Map> mapDB;
		std::vector<std::string> mapNames;
	};
}