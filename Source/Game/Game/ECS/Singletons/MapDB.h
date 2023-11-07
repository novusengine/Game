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

	public:
		robin_hood::unordered_map<u32, u32> mapInternalNameHashToID;
	};
}