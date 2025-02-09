#pragma once
#include "Game-Lib/Gameplay/Animation/Defines.h"

#include <Base/Types.h>

#include <entt/fwd.hpp>

namespace ECS::Components
{
    struct NetworkedEntity
    {
        entt::entity networkID;
        entt::entity targetEntity;
        u32 bodyID = std::numeric_limits<u32>().max();
        ::Animation::Defines::Type overrideAnimation = ::Animation::Defines::Type::Invalid;

        vec3 initialPosition = vec3(0.0f);
        vec3 desiredPosition = vec3(0.0f);
        f32 positionProgress = -1.0f;
        
        bool positionOrRotationIsDirty = false;
    };
}