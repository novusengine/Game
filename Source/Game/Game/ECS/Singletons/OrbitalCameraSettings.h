#pragma once
#include <Base/Types.h>
#include <entt/fwd.hpp>

namespace ECS::Singletons
{
    struct OrbitalCameraSettings
    {
    public:
        entt::entity entity;
        entt::entity entityToTrack;

        bool captureMouse;
        bool captureMouseHasMoved;

        vec2 prevMousePosition;
        f32 mouseSensitivity = 0.05f;

        vec3 cameraCurrentZoomOffset = vec3(0.0f, 0.0f, 0.0f);
        vec3 cameraTargetZoomOffset = vec3(0.0f, 0.0f, 0.0f);
        f32 cameraZoomSpeed = 0.5f;
        f32 cameraZoomProgress = 1.0f;

        u8 cameraZoomLevel = 3;

    public:
        vec2 GetZoomLevel()
        {
            switch (cameraZoomLevel)
            {
                case 0: return vec2(1.8f, 0.0f);
                case 1: return vec2(1.8f, -2.0f);
                case 2: return vec2(1.8f, -4.0f);
                case 3: return vec2(1.8f, -6.0f);
                case 4: return vec2(1.8f, -8.0f);

                default: break;
            }

            return vec2(1.0f);
        };
    };
}