#pragma once
#include <Base/Types.h>

namespace ECS::Util
{
    namespace CameraUtil
    {
        void SetCaptureMouse(bool capture);
        void SetCaptureMouse(bool capture, const vec2& restorePosition);
        bool IsCapturingMouse();
        f32 GetMouseSensitivity();
        void CenterOnObject(const vec3& position, f32 radius);
        void CalculatePosRotForMatrix(const mat4x4& targetMatrix, const vec3& cameraEulerAngles, f32 cameraHeightOffset, f32 cameraZoomDistance, vec3& resultPosition, quat& resultRotation);
    }
}
