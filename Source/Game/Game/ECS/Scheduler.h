#pragma once
#include <Base/Types.h>

#include <entt/fwd.hpp>

namespace ECS
{
    class Scheduler
    {
    public:
        Scheduler();

        void Init(entt::registry & registry);
        void Update(entt::registry& registry, f32 deltaTime);

    private:
    };
}