#pragma once
#include <Base/Types.h>
#include <entt/fwd.hpp>

class KeybindGroup;

namespace ECS::Systems
{
    class OrbitalCamera
    {
    public:
        static void Init(entt::registry& registry);
        static void Update(entt::registry& registry, f32 deltaTime);

        static void CapturedMouseMoved(entt::registry& registry, const vec2& position);
        static void CapturedMouseScrolled(entt::registry& registry, const vec2& position);

    private:
        static KeybindGroup* _keybindGroup;
    };
}