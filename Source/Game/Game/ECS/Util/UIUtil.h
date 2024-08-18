#pragma once
#include <Base/Types.h>

#include <entt/entt.hpp>

namespace ECS::Util
{
	namespace UI
	{
		entt::entity GetOrEmplaceCanvas(entt::registry* registry, const char* name, vec2 pos, ivec2 size);
		entt::entity CreateCanvas(entt::registry* registry, const char* name, vec2 pos, ivec2 size, entt::entity parent = entt::null);

		entt::entity CreatePanel(entt::registry* registry, vec2 pos, ivec2 size, u32 layer, const char* templateName, entt::entity parent);
		entt::entity CreateText(entt::registry* registry, const char* text, vec2 pos, u32 layer, const char* templateName, entt::entity parent);
	}
}