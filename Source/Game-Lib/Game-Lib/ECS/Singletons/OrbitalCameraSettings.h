#pragma once
#include <Base/Types.h>
#include <entt/entity/entity.hpp>

namespace ECS::Singletons
{
    struct OrbitalCameraSettings
    {
    public:
        entt::entity entity = entt::null;

        bool captureMouse = false;
        bool captureMousePending = false;
        bool captureMouseHasMoved = false;
        bool captureMouseWasDragged = false;

        bool mouseLeftDown = false;
        bool mouseRightDown = false;

        vec2 captureStartMousePosition = vec2(0.0f);
        vec2 captureRestoreMousePosition = vec2(0.0f);
        f64 captureStartTime = 0.0;

        vec3 cameraCurrentZoomOffset = vec3(0.0f, 0.0f, 0.0f);
        vec3 cameraTargetZoomOffset = vec3(0.0f, 0.0f, 0.0f);
        f32 cameraZoomSpeed = 0.5f;
        f32 cameraZoomProgress = 1.0f;

        f32 cameraCollisionCurrentDistance = -1.0f;
        bool cameraCollisionWasObstructed = false;
    };
}
