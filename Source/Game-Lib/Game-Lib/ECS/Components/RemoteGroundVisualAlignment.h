#pragma once

#include <Base/Types.h>

namespace ECS::Components
{
    struct RemoteGroundVisualAlignment
    {
    public:
        vec3 smoothedNormal = vec3(0.0f, 1.0f, 0.0f);
    };
}
