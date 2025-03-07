#pragma once
#include "ClientDBSingleton.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Base/Types.h>
#include <Base/Container/StringTable.h>

#include <FileFormat/Novus/ClientDB/ClientDB.h>
#include <FileFormat/Novus/ClientDB/Definitions.h>

#include <entt/entt.hpp>
#include <robinhood/robinhood.h>

namespace ECS
{
    namespace Singletons
    {
        struct MapSingleton
        {
        public:
            MapSingleton() { }

        public:
            robin_hood::unordered_map<u32, u32> mapInternalNameHashToID;
        };
    }
}