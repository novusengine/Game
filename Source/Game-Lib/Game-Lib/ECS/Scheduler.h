#pragma once
#include <Base/Types.h>

#include <entt/fwd.hpp>

struct EnttRegistries;

namespace ECS
{
    class Scheduler
    {
    public:
        Scheduler();

        void Init(EnttRegistries& registries);
        void Update(EnttRegistries& registries, f32 deltaTime);

    private:
    };
}