#pragma once
#include <Base/Types.h>

namespace ECS::Components
{
    struct UnitMovementOverTime
    {
    public:
        vec3 startPos = vec3(0.0f);
        vec3 endPos = vec3(0.0f);
        f32 time = 1.0f;
    };
}