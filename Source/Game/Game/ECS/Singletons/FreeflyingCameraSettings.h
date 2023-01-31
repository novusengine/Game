#pragma once
#include <Base/Types.h>
#include <entt/fwd.hpp>

namespace ECS::Singletons
{
	struct FreeflyingCameraSettings
	{
	public:
		bool captureMouse;
		bool captureMouseHasMoved;

		vec2 prevMousePosition;

		f32 mouseSensitivity = 0.05f;
		f32 cameraSpeed = 150.0f;
	};
}