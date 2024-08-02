#pragma once
#include <Base/Types.h>
#include <entt/fwd.hpp>

namespace ECS::Singletons
{
    struct OrbitalCameraSettings
    {
    public:
        entt::entity entity;

        bool captureMouse;
        bool captureMouseHasMoved;

        bool mouseLeftDown;
        bool mouseRightDown;

        vec2 prevMousePosition;
        f32 mouseSensitivity = 0.05f;

        vec3 cameraCurrentZoomOffset = vec3(0.0f, 0.0f, 0.0f);
        vec3 cameraTargetZoomOffset = vec3(0.0f, 0.0f, 0.0f);
        f32 cameraZoomSpeed = 0.5f;
        f32 cameraZoomProgress = 1.0f;
    };
}