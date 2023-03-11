#include "UpdateScripts.h"

#include "Game/Scripting/LuaManager.h"
#include "Game/Util/ServiceLocator.h"

namespace ECS::Systems
{
	void UpdateScripts::Init(entt::registry& registry)
	{
	}

	void UpdateScripts::Update(entt::registry& registry, f32 deltaTime)
	{
		Scripting::LuaManager* luaManager = ServiceLocator::GetLuaManager();
		luaManager->Update(deltaTime);
	}
}