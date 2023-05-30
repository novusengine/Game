#pragma once
#include <Base/Types.h>
#include <Base/Util/Reflection.h>

namespace ECS::Components
{
	struct DirtyTransform 
	{
	public:
		u64 dirtyFrame = 0;
	};

	struct Transform
	{
	public:
		bool isDirty = false;
		vec3 position = vec3(0.0f, 0.0f, 0.0f);
		quat rotation = quat(0.0f, 0.0f, 0.0f, 1.0f);
		vec3 scale = vec3(1.0f, 1.0f, 1.0f);

		vec3 forward = vec3(0.0f, 0.0f, 1.0f);
		vec3 right = vec3(1.0f, 0.0f, 0.0f);
		vec3 up = vec3(0.0f, 1.0f, 0.0f);

		mat4x4 matrix = mat4x4(1.0f);

		// We are using Unitys Right Handed coordinate system
		// +X = right
		// +Y = up
		// +Z = forward
		static const vec3 WORLD_FORWARD;
		static const vec3 WORLD_RIGHT;
		static const vec3 WORLD_UP;
	};
}

REFL_TYPE(ECS::Components::Transform)
	REFL_FIELD(isDirty, Reflection::Hidden())
	REFL_FIELD(position)
	REFL_FIELD(rotation)
	REFL_FIELD(scale, Reflection::DragSpeed(0.1f))

	REFL_FIELD(forward, Reflection::Hidden())
	REFL_FIELD(right, Reflection::Hidden())
	REFL_FIELD(up, Reflection::Hidden())
	REFL_FIELD(matrix, Reflection::Hidden())
REFL_END