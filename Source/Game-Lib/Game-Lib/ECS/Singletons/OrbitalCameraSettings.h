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
        bool captureMousePending;
        bool captureMouseHasMoved;
        bool captureMouseWasDragged;

        bool mouseLeftDown;
        bool mouseRightDown;

        vec2 prevMousePosition;
        vec2 captureStartMousePosition;
        vec2 captureRestoreMousePosition;
        f64 captureStartTime = 0.0;

        vec3 cameraCurrentZoomOffset = vec3(0.0f, 0.0f, 0.0f);
        vec3 cameraTargetZoomOffset = vec3(0.0f, 0.0f, 0.0f);
        f32 cameraZoomSpeed = 0.5f;
        f32 cameraZoomProgress = 1.0f;

        f32 cameraCollisionCurrentDistance = -1.0f;
        bool cameraCollisionWasObstructed = false;
    };
}
