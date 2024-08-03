#pragma once
#include <Base/Types.h>
#include <entt/fwd.hpp>

namespace ECS::Systems
{
	class UpdateDayNightCycle
	{
	public:
		static void Init(entt::registry& registry);
		static void Update(entt::registry& registry, f32 deltaTime);

		static void SetTimeToDefault(entt::registry& registry);
		static void SetTime(entt::registry& registry, f32 time);
		static void SetSpeedModifier(entt::registry& registry, f32 speedModifier);
		static void SetTimeAndSpeedModifier(entt::registry& registry, f32 time, f32 speedModifier);
	};
}