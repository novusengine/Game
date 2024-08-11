#pragma once
#include <Base/Types.h>

#include <robinhood/robinhood.h>

namespace ECS::Singletons
{
    struct TextureSingleton
    {
    public:
        TextureSingleton() {}

        robin_hood::unordered_map<u32, std::string> textureHashToPath;
        robin_hood::unordered_map<u32, u32> textureHashToTextureID;
    };
}