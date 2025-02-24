#pragma once
#include <Base/Types.h>

#include <Gameplay/GameDefine.h>

#include <limits>

namespace ECS::Components
{
    struct Item
    {
    public:
        GameDefine::ObjectGuid guid = GameDefine::ObjectGuid::Empty;
        u32 itemID = std::numeric_limits<u32>::max();
        u16 count = 1;
        u16 durability = 0;
    };
}