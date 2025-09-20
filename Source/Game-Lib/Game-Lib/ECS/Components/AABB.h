#pragma once
#include <Base/Types.h>

namespace ECS::Components
{
    struct AABB
    {
    public:
        vec3 centerPos;
        vec3 extents;
    };

    struct WorldAABB
    {
    public:
        vec3 min;
        vec3 max;
    };
}