#include "LuaSystemBase.h"
#include "Game/Scripting/LuaDefines.h"
#include "Game/Scripting/LuaManager.h"
#include "Game/Scripting/Handlers/GameEventHandler.h"
#include "Game/Util/ServiceLocator.h"

#include <Base/Util/DebugHandler.h>

#include <lualib.h>

#include <robinhood/robinhood.h>
#include <Game/Scripting/LuaStateCtx.h>

namespace Scripting
{
	LuaSystemBase::LuaSystemBase(u32 numStates) : _events()
	{
		Init(numStates);
	}

	void LuaSystemBase::Init(u32 numStates)
	{
		u32 numCurrentStates = static_cast<u32>(_states.size());

		for (u32 i = 0; i < numCurrentStates; i++)
		{
			DestroyState(i);
		}

		_states.clear();
		_states.reserve(numStates);

		for (u32 i = 0; i < numStates; i++)
		{
			CreateState();
		}
	}

	void LuaSystemBase::Update(f32 deltaTime)
	{
		LuaSystemEvent systemEvent;
		while (_events.try_dequeue(systemEvent))
		{
			switch (systemEvent)
			{
				case LuaSystemEvent::Reload:
				{
					u32 numStates = static_cast<u32>(_states.size());
					Init(numStates);

					break;
				}

				default: break;
			}
		}
	}

	void LuaSystemBase::PushEvent(LuaSystemEvent systemEvent)
	{
		_events.enqueue(systemEvent);
	}

	lua_State* LuaSystemBase::CreateState()
	{
		LuaManager* luaManager = ServiceLocator::GetLuaManager();

		LuaStateCtx ctx(luaL_newstate());

		ctx.RegisterDefaultLibraries();

		// Set Globals
		{
			const LuaTable& table = luaManager->GetGlobalTable();
			ctx.SetGlobal(table);
		}

		ctx.MakeReadOnly();

		const std::vector<LuaBytecodeEntry>& bytecodeList = luaManager->GetBytecodeList();
		for (u32 j = 0; j < bytecodeList.size(); j++)
		{
			const LuaBytecodeEntry& bytecodeEntry = bytecodeList[j];
			std::string filePath = bytecodeEntry.filePath + "/" + bytecodeEntry.fileName;

			i32 result = ctx.LoadBytecode(filePath.c_str(), bytecodeEntry.bytecode, 0);
			if (result != LUA_OK)
			{
				ctx.Pop();
			}
		}

		auto gameEventHandler = luaManager->GetLuaHandler<GameEventHandler*>(LuaHandlerType::GameEvent);

		lua_State* state = ctx.GetState();
		gameEventHandler->SetupEvents(state);

		i32 top = ctx.GetTop();
		for (i32 i = 0; i < top; i++)
		{
			ctx.Resume();

			i32 result = ctx.GetStatus();
			if (result != LUA_OK)
			{
				ctx.ReportError();
				break;
			}
		}

		_states.push_back(state);
		return state;
	}

	bool LuaSystemBase::DestroyState(u32 index)
	{
		if (index >= _states.size())
			return false;

		lua_State* state = _states[index];
		LuaStateCtx ctx(state);

		LuaManager* luaManager = ServiceLocator::GetLuaManager();
		auto gameEventHandler = luaManager->GetLuaHandler<GameEventHandler*>(LuaHandlerType::GameEvent);
		gameEventHandler->ClearEvents(ctx.GetState());

		ctx.Close();

		return true;
	}
}