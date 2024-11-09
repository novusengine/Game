#pragma once
#include <Base/Types.h>
#include <entt/fwd.hpp>

namespace ECS::Systems::UI
{
	class UpdateBoundingRects
	{
	public:
		static void Update(entt::registry& registry, f32 deltaTime);
	};
}