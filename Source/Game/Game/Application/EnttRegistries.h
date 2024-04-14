#pragma once
#include <entt/fwd.hpp>

struct EnttRegistries
{
	entt::registry* gameRegistry;
	entt::registry* eventIncomingRegistry;
	entt::registry* eventOutgoingRegistry;
};