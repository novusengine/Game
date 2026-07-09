#pragma once
#include <Base/Types.h>
#include <entt/fwd.hpp>

namespace ECS::Systems
{
    // Quantizes the time of day used for the shadow sun direction, see shadowSunUpdateInterval
    f32 GetShadowTimeOfDay(f32 timeOfDay);

    class CalculateShadowCameraMatrices
    {
    public:
        static void Update(entt::registry& registry, f32 deltaTime);
    };
}