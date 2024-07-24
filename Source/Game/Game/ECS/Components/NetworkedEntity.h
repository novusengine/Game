#pragma once
#include <Base/Types.h>

#include <entt/fwd.hpp>

namespace ECS::Components
{
	struct NetworkedEntity
	{
		entt::entity networkID;
		entt::entity targetEntity;
        u32 bodyID = std::numeric_limits<u32>().max();

		vec3 initialPosition = vec3(0.0f);
		vec3 desiredPosition = vec3(0.0f);
		f32 positionProgress = -1.0f;

        bool positionOrRotationChanged = false;

        // These rotation offsets are specifically for Y axis only
        vec4 spineRotationSettings = vec4(0.0f);    // x = current, y = target, z = time to change, w = time since last change
        vec4 headRotationSettings = vec4(0.0f);	    // x = current, y = target, z = time to change, w = time since last change
        vec4 waistRotationSettings = vec4(0.0f);    // x = current, y = target, z = time to change, w = time since last change
	};
}