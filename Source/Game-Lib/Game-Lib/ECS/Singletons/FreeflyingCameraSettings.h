#pragma once
#include <Base/Types.h>
#include <entt/fwd.hpp>

namespace ECS::Singletons
{
    struct FreeflyingCameraSettings
    {
    public:
        entt::entity entity;

        bool captureMouse;
        bool captureMouseHasMoved;

        vec2 prevMousePosition;

        f32 cameraSpeed = 150.0f;
    };
}
