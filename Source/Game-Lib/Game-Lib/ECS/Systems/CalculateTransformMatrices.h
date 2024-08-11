#pragma once
#include <Base/Types.h>
#include <entt/fwd.hpp>

#include <unordered_set>

namespace ECS::Systems
{
	class CalculateTransformMatrices
	{
	public:
		static void Update(entt::registry& registry, f32 deltaTime);

	private:
		static std::unordered_set<entt::entity> _entitiesToUndirty;
	};
}