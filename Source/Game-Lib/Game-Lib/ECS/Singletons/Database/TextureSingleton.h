#pragma once
#include <Base/Types.h>

#include <robinhood/robinhood.h>

namespace ECS
{
    namespace Singletons
    {
        struct TextureSingleton
        {
        public:
            TextureSingleton() {}

            robin_hood::unordered_map<u32, std::vector<u64>> materialResourcesIDToTextureHashes;
        };
    }
}