#pragma once
#include <entt/entity/entity.hpp>

namespace ECS::Singletons
{
    struct ActiveCamera
    {
    public:
        entt::entity entity = entt::null;
    };
}
