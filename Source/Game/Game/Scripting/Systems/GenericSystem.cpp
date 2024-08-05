#include "GenericSystem.h"
#include "Game/Scripting/LuaDefines.h"
#include "Game/Scripting/LuaManager.h"
#include "Game/Scripting/LuaState.h"
#include "Game/Scripting/Handlers/GameEventHandler.h"
#include "Game/Util/ServiceLocator.h"

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