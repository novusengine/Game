#pragma once
#include <Base/Types.h>
#include <entt/fwd.hpp>

namespace ECS::Systems
{
    class Animations
    {
    public:
        static void Init(entt::registry& registry);
        static void UpdateSimulation(entt::registry& registry, f32 deltaTime);
    };
}