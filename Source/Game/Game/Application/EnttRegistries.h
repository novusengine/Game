#pragma once
#include <entt/fwd.hpp>

struct EnttRegistries
{
	entt::registry* gameRegistry;
	entt::registry* uiRegistry;
	entt::registry* eventIncomingRegistry;
	entt::registry* eventOutgoingRegistry;
};