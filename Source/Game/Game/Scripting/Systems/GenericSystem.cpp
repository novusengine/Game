#include "GenericSystem.h"
#include "Game/Scripting/LuaDefines.h"
#include "Game/Scripting/LuaManager.h"
#include "Game/Scripting/LuaStateCtx.h"
#include "Game/Scripting/Handlers/GameEventHandler.h"
#include "Game/Util/ServiceLocator.h"

#include <Base/Util/DebugHandler.h>

namespace Scripting
{
	GenericSystem::GenericSystem(u32 numStates) : LuaSystemBase(numStates) { }

	void GenericSystem::Prepare(f32 deltaTime)
	{
		LuaManager* luaManager = ServiceLocator::GetLuaManager();
		auto gameEventHandler = luaManager->GetLuaHandler<GameEventHandler*>(LuaHandlerType::GameEvent);

		LuaGameEventUpdatedData eventData;
		eventData.deltaTime = deltaTime;
		gameEventHandler->CallEvent(_states[0], static_cast<u32>(LuaGameEvent::Updated), &eventData);
	}

	void GenericSystem::Run(f32 deltaTime, u32 index)
	{
		LuaStateCtx ctx(_states[index]);

		// TODO :: Do Stuff
	}
}