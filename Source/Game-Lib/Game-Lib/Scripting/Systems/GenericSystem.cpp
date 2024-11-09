#include "GenericSystem.h"
#include "Game-Lib/Scripting/LuaDefines.h"
#include "Game-Lib/Scripting/LuaManager.h"
#include "Game-Lib/Scripting/LuaState.h"
#include "Game-Lib/Scripting/Handlers/GameEventHandler.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Base/Util/DebugHandler.h>

namespace Scripting
{
    GenericSystem::GenericSystem() : LuaSystemBase() { }

    void GenericSystem::Prepare(f32 deltaTime, lua_State* state)
    {
        LuaManager* luaManager = ServiceLocator::GetLuaManager();
        auto gameEventHandler = luaManager->GetLuaHandler<GameEventHandler*>(LuaHandlerType::GameEvent);

        LuaGameEventUpdatedData eventData;
        eventData.deltaTime = deltaTime;
        gameEventHandler->CallEvent(state, static_cast<u32>(LuaGameEvent::Updated), &eventData);
    }

    void GenericSystem::Run(f32 deltaTime, lua_State* state)
    {
        LuaState ctx(state);

        // TODO :: Do Stuff
    }
}