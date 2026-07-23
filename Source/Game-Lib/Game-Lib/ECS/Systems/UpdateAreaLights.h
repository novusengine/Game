#pragma once
#include <Base/Types.h>
#include <entt/fwd.hpp>

namespace ECS::Systems
{
    // Quantizes the time of day used for the shadow sun direction, see shadowSunUpdateInterval
    f32 GetShadowTimeOfDay(f32 timeOfDay);

    class UpdateAreaLights
    {
    public:
        static void Init(entt::registry& registry);
        static void Update(entt::registry& registry, f32 deltaTime);

        static vec3 GetLightDirection(f32 timeOfDay);
    };
}