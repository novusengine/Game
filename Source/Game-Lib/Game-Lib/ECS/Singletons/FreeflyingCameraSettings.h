#pragma once
#include <Base/Types.h>
#include <entt/entity/entity.hpp>

namespace ECS::Singletons
{
    struct FreeflyingCameraSettings
    {
    public:
        entt::entity entity = entt::null;

        bool captureMouse = false;
        bool captureMouseHasMoved = false;

        f32 cameraSpeed = 150.0f;
    };
}
