#pragma once
#include "Game-Lib/Gameplay/Animation/Defines.h"

#include <Base/Types.h>

#include <Gameplay/GameDefine.h>

#include <entt/fwd.hpp>

namespace ECS::Components
{
    struct Unit
    {
    public:
        GameDefine::ObjectGuid networkID;
        entt::entity targetEntity;
        GameDefine::UnitClass unitClass;
        GameDefine::UnitRace race;
        GameDefine::UnitGender gender;

        u32 bodyID = std::numeric_limits<u32>().max();
        ::Animation::Defines::Type overrideAnimation = ::Animation::Defines::Type::Invalid;

        bool positionOrRotationIsDirty = false;
    };
}