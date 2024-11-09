#pragma once
#include <Base/Types.h>

namespace ECS::Components::UI
{
    struct BoundingRect
    {
    public:
        vec2 min;
        vec2 max;
    };
}