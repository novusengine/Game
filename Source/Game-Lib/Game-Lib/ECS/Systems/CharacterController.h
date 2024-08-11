#pragma once
#include <Base/Types.h>
#include <entt/fwd.hpp>

namespace ECS::Systems
{
    class CharacterController
    {
    public:
        static void Init(entt::registry& registry);
        static void Update(entt::registry& registry, f32 deltaTime);

        static void InitCharacterController(entt::registry& registry, bool isLocal);
        static void DeleteCharacterController(entt::registry& registry, bool isLocal);
    };
}