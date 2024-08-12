#pragma once
#include "LuaHandlerBase.h"

#include <Base/Types.h>

#include <robinhood/robinhood.h>

#include <vector>

struct lua_State;

namespace Scripting
{
	struct LuaEventData;

	class LuaEventHandlerBase : public LuaHandlerBase
	{
	public:
		using EventHandlerFn = std::function<void(lua_State*, u32, LuaEventData*)>;

		LuaEventHandlerBase(u32 numEvents) : LuaHandlerBase(), _numEvents(numEvents)
		{
			_eventToLuaStateFuncRefList.resize(numEvents);
			_eventToFuncHandlerList.resize(numEvents);

			Clear();
		}

		void Clear()
		{
			for (u32 i = 0; i < _eventToLuaStateFuncRefList.size(); i++)
			{
				robin_hood::unordered_map<u64, std::vector<i32>>& luaStateToFuncRefList = _eventToLuaStateFuncRefList[i];

				luaStateToFuncRefList.clear();
				luaStateToFuncRefList.reserve(32);
			}

			for (u32 i = 0; i < _eventToFuncHandlerList.size(); i++)
			{
				_eventToFuncHandlerList[i] = nullptr;
			}
		}

		virtual void RegisterEventCallback(lua_State* state, u32 eventID, i32 funcHandle) = 0;
		virtual void SetEventHandler(u32 eventID, EventHandlerFn fn) = 0;
		virtual void CallEvent(lua_State* state, u32 eventID, LuaEventData* data) = 0;

		void SetupEvents(lua_State* state)
		{
			u64 key = reinterpret_cast<u64>(state);

			for (u32 i = 0; i < _eventToLuaStateFuncRefList.size(); i++)
			{
				robin_hood::unordered_map<u64, std::vector<i32>>& luaStateToFuncRefList = _eventToLuaStateFuncRefList[i];

				std::vector<i32>& funcRefList = luaStateToFuncRefList[key];
				funcRefList.clear();
				funcRefList.reserve(16);
			}
		}
		void ClearEvents(lua_State* state)
		{
			u64 key = reinterpret_cast<u64>(state);

			for (u32 i = 0; i < _eventToLuaStateFuncRefList.size(); i++)
			{
				robin_hood::unordered_map<u64, std::vector<i32>>& luaStateToFuncRefList = _eventToLuaStateFuncRefList[i];

				luaStateToFuncRefList.erase(key);
			}
		}

	protected:
		const u32 _numEvents;

		std::vector<robin_hood::unordered_map<u64, std::vector<i32>>> _eventToLuaStateFuncRefList;
		std::vector<EventHandlerFn> _eventToFuncHandlerList;
	};
}