#pragma once
#include <Base/Types.h>

namespace ECS
{
    namespace Singletons
    {
        struct TextureSingleton;
    }
}

namespace ECSUtil::Texture
{
    bool Refresh();

    bool HasTexture(ECS::Singletons::TextureSingleton& textureSingleton, u32 textureHash);
    const std::string& GetTexturePath(ECS::Singletons::TextureSingleton& textureSingleton, u32 textureHash);
}