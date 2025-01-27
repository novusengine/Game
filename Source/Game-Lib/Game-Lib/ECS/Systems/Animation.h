#pragma once
#include <Base/Types.h>
#include <entt/fwd.hpp>

namespace ECS::Systems
{
    class Animation
    {
    public:
        static void Init(entt::registry& registry);
        static void Update(entt::registry& registry, f32 deltaTime);

    private:
        static void HandleAnimationDataInit(entt::registry& registry, f32 deltaTime);
        static void HandleSimulation(entt::registry& registry, f32 deltaTime);
    };
}