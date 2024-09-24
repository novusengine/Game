#pragma once
#include <Base/Types.h>

namespace ECS::Util
{
    namespace CameraUtil
    {
        void SetCaptureMouse(bool capture);
        void CenterOnObject(const vec3& position, f32 radius);
    }
}