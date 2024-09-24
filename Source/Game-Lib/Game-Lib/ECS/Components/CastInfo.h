#pragma once
#include <Base/Types.h>

#include <entt/fwd.hpp>

namespace ECS::Components
{
    struct CastInfo
    {
        entt::entity target;

        f32 castTime = 0.0f;
        f32 duration = 0.0f;
    };
}