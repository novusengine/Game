#pragma once
#include <Base/Types.h>

#include <robinhood/robinhood.h>

namespace ECS
{
    namespace Singletons
    {
        struct SpellSingleton
        {
        public:
            SpellSingleton() {}

            robin_hood::unordered_map<u32, std::vector<u32>> spellIDToEffectList;
        };
    }
}