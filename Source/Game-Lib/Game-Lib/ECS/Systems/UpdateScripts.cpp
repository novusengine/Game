#include "UpdateScripts.h"

#include "Game-Lib/ECS/Singletons/RenderState.h"
#include "Game-Lib/Editor/TerrainEditSession.h"
#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Scripting/LuaManager.h>

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

        auto& renderState = registry.ctx().get<ECS::Singletons::RenderState>();

        Editor::TerrainEditSession* terrainEditSession = ServiceLocator::GetGameRenderer()->GetTerrainEditSession();
        if (terrainEditSession)
            terrainEditSession->Update(deltaTime);

        Scripting::LuaManager* luaManager = ServiceLocator::GetLuaManager();
        luaManager->Update(deltaTime, renderState.frameNumber);
    }
}
