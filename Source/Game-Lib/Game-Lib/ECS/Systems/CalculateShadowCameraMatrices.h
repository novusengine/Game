#pragma once
#include <Base/Types.h>
#include <entt/fwd.hpp>

namespace ECS::Systems
{
    class CalculateShadowCameraMatrices
    {
    public:
        static void Update(entt::registry& registry, f32 deltaTime);
    };
}