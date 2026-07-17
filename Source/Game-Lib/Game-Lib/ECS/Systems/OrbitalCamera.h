#pragma once
#include "Game-Lib/Input/InputActionSystem.h"

#include <Base/Types.h>
#include <entt/fwd.hpp>

namespace ECS::Systems
{
    class OrbitalCamera
    {
    public:
        static void Init(entt::registry& registry);
        static void ResetForNewWorld(entt::registry& registry);
        static void Update(entt::registry& registry, f32 deltaTime);

        static void CapturedMouseMoved(entt::registry& registry, const vec2& delta);
        static void CapturedMouseScrolled(entt::registry& registry, const vec2& delta);

    private:
        static InputActionContextHandle _inputContext;
        static InputContextHandle _pointerInputContext;
        static InputActionHandle _altAction;
    };
}
