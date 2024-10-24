#include "UpdateScripts.h"

#include "Game-Lib/Scripting/LuaManager.h"
#include "Game-Lib/Util/ServiceLocator.h"

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