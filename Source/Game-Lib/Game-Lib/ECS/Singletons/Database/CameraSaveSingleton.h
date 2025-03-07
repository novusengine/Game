#pragma once
#include "ClientDBSingleton.h"
#include "Game-Lib/Gameplay/Database/Shared.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Base/Types.h>

#include <FileFormat/Novus/ClientDB/ClientDB.h>
#include <FileFormat/Novus/ClientDB/Definitions.h>

#include <entt/entt.hpp>
#include <robinhood/robinhood.h>

namespace ECS
{
    namespace Singletons
    {
        struct CameraSaveSingleton
        {
        public:
            CameraSaveSingleton() { }

        public:
            robin_hood::unordered_map<u32, u32> cameraSaveNameHashToID;
        };
    }
}