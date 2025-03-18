#pragma once
#include <Base/Types.h>

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