#include "UpdateScripts.h"

#include "Game-Lib/Scripting/LuaManager.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <entt/entt.hpp>
#include <tracy/Tracy.hpp>

namespace ECS::Systems
{
    void UpdateScripts::Init(entt::registry& registry)
    {
    }

    void UpdateScripts::Update(entt::registry& registry, f32 deltaTime)
    {
        ZoneScopedN("ECS::UpdateScripts");

        Scripting::LuaManager* luaManager = ServiceLocator::GetLuaManager();
        luaManager->Update(deltaTime);
    }
}