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
        ObjectGUID networkID;
        std::string name;

        entt::entity targetEntity;
        GameDefine::UnitClass unitClass;
        GameDefine::UnitRace race;
        GameDefine::UnitGender gender;
        f32 scale = 1.0f;

        u32 bodyID = std::numeric_limits<u32>().max();
        ::Animation::Defines::Type overrideAnimation = ::Animation::Defines::Type::Invalid;

        bool positionOrRotationIsDirty = false;
        bool isAutoAttacking = false;

        ::Animation::Defines::Type attackReadyAnimation = ::Animation::Defines::Type::Invalid;
        ::Animation::Defines::Type attackMainHandAnimation = ::Animation::Defines::Type::Invalid;
        ::Animation::Defines::Type attackOffHandAnimation = ::Animation::Defines::Type::Invalid;
    };
}