#pragma once
#include <Base/Types.h>
#include <entt/fwd.hpp>

namespace ECS::Systems
{
	class UpdateAABBs
	{
	public:
		static void Update(entt::registry& registry, f32 deltaTime);
	};
}