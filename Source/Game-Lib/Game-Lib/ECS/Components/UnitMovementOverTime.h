#pragma once
#include <Base/Types.h>

namespace ECS::Components
{
    struct UnitMovementOverTime
    {
    public:
        vec3 startPos = vec3(0.0f);
        vec3 endPos = vec3(0.0f);
        vec3 previousRenderedPos = vec3(0.0f);
        f64 lastSnapshotTime = 0.0;
        f32 elapsed = 0.0f;
        f32 duration = 0.1f;
        f32 displayedSpeed = 0.0f;
        bool hasSnapshot = false;
        bool hasRenderedPosition = false;
    };
}
