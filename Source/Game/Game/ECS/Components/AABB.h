#pragma once

namespace ECS::Components
{
	struct AABB
	{
		vec3 centerPos;
		vec3 extents;
	};

	struct WorldAABB
	{
		vec3 min;
		vec3 max;
	};
}