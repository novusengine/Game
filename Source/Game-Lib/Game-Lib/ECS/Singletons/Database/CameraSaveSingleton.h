#pragma once
#include <Base/Types.h>

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