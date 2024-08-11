#include "GameEventHandler.h"
#include "Game-Lib/Scripting/LuaStateCtx.h"

#include "Game-Lib/Scripting/LuaManager.h"
#include "Game-Lib/Scripting/Systems/LuaSystemBase.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <lualib.h>

namespace Scripting
{
	void GameEventHandler::Register()
	{
		LuaManager* luaManager = ServiceLocator::GetLuaManager();

		// Register Functions
		{
			luaManager->SetGlobal("RegisterGameEvent", GameEventHandler::RegisterGameEvent, true);
		}

		// Set Event Handlers
		{
			SetEventHandler(static_cast<u32>(LuaGameEvent::Loaded), std::bind(&GameEventHandler::OnGameLoaded, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
			SetEventHandler(static_cast<u32>(LuaGameEvent::Updated), std::bind(&GameEventHandler::OnGameUpdated, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
		}

		CreateGameEventTable();
	}

	void GameEventHandler::SetEventHandler(u32 eventID, EventHandlerFn fn)
	{
		LuaGameEvent gameEventID = static_cast<LuaGameEvent>(eventID);

		if (gameEventID == LuaGameEvent::Invalid || gameEventID >= LuaGameEvent::Count)
		{
			return;
		}

		_eventToFuncHandlerList[eventID] = fn;
	}
	void GameEventHandler::CallEvent(lua_State* state, u32 eventID, LuaEventData* data)
	{
		LuaGameEvent gameEventID = static_cast<LuaGameEvent>(eventID);

		if (gameEventID == LuaGameEvent::Invalid || gameEventID >= LuaGameEvent::Count)
		{
			return;
		}

		_eventToFuncHandlerList[eventID](state, eventID, data);
	}
	void GameEventHandler::RegisterEventCallback(lua_State* state, u32 eventID, i32 funcHandle)
	{
		LuaManager* luaManager = ServiceLocator::GetLuaManager();

		u32 id = eventID;
		u64 key = reinterpret_cast<u64>(state);

		std::vector<i32>& funcRefList = _eventToLuaStateFuncRefList[id][key];
		funcRefList.push_back(funcHandle);
	}
	
	i32 GameEventHandler::RegisterGameEvent(lua_State* state)
	{
		LuaStateCtx ctx(state);

		u32 eventID = ctx.GetU32(0, 1);

		LuaGameEvent gameEventID = static_cast<LuaGameEvent>(eventID);
		if (gameEventID == LuaGameEvent::Invalid || gameEventID >= LuaGameEvent::Count)
		{
			return 0;
		}

		if (!lua_isfunction(ctx.GetState(), 2))
		{
			return 0;
		}

		i32 funcHandle = ctx.GetRef(2);

		LuaManager* luaManager = ServiceLocator::GetLuaManager();
		auto gameEventHandler = luaManager->GetLuaHandler<GameEventHandler*>(LuaHandlerType::GameEvent);

		gameEventHandler->RegisterEventCallback(state, eventID, funcHandle);

		return 0;
	}

	i32 GameEventHandler::OnGameLoaded(lua_State* state, u32 eventID, LuaEventData* data)
	{
		LuaManager* luaManager = ServiceLocator::GetLuaManager();
		auto eventData = reinterpret_cast<LuaGameEventLoadedData*>(data);

		u32 id = eventID;
		u64 key = reinterpret_cast<u64>(state);

		LuaStateCtx ctx(state);

		std::vector<i32>& funcRefList = _eventToLuaStateFuncRefList[id][key];
		for (i32 funcHandle : funcRefList)
		{
			ctx.PushLFunction(funcHandle, false);
			ctx.PushNumber(id);
			ctx.PushString(eventData->motd.c_str());
			ctx.PCall();
		}

		return 0;
	}

	i32 GameEventHandler::OnGameUpdated(lua_State* state, u32 eventID, LuaEventData* data)
	{
		LuaManager* luaManager = ServiceLocator::GetLuaManager();
		auto eventData = reinterpret_cast<LuaGameEventUpdatedData*>(data);

		u32 id = eventID;
		u64 key = reinterpret_cast<u64>(state);

		LuaStateCtx ctx(state);

		std::vector<i32>& funcRefList = _eventToLuaStateFuncRefList[id][key];
		for (i32 funcHandle : funcRefList)
		{
			ctx.PushLFunction(funcHandle, false);
			ctx.PushNumber(id);
			ctx.PushNumber(eventData->deltaTime);
			ctx.PCall();
		}

		return 0;
	}

	void GameEventHandler::CreateGameEventTable()
	{
		LuaManager* luaManager = ServiceLocator::GetLuaManager();

		LuaTable table = 
		{
			{
				{ "Invalid", "Invalid" },
				{ "Loaded", 1u },
				{ "Updated", 2u },
				{ "Count", 3u },
			}
		};

		luaManager->SetGlobal("GameEvent", table, true);
	}
}
