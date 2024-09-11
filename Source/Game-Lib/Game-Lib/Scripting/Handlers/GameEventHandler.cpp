#include "GameEventHandler.h"
#include "Game-Lib/Scripting/LuaState.h"
#include "Game-Lib/Scripting/LuaManager.h"
#include "Game-Lib/Scripting/Systems/LuaSystemBase.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <lualib.h>

namespace Scripting
{
    void GameEventHandler::Register(lua_State* state)
    {
        // Register Functions
        {
            LuaMethodTable::Set(state, gameEventMethods);
        }

        // Set Event Handlers
        {
            SetEventHandler(static_cast<u32>(LuaGameEvent::Loaded), std::bind(&GameEventHandler::OnGameLoaded, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
            SetEventHandler(static_cast<u32>(LuaGameEvent::Updated), std::bind(&GameEventHandler::OnGameUpdated, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
        }

        CreateGameEventTable(state);
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
        if (eventID >= _eventToFuncHandlerList.size())
            return;

        LuaGameEvent gameEventID = static_cast<LuaGameEvent>(eventID);

        if (gameEventID == LuaGameEvent::Invalid || gameEventID >= LuaGameEvent::Count)
        {
            return;
        }

        EventHandlerFn& fn = _eventToFuncHandlerList[eventID];
        if (fn)
            fn(state, eventID, data);
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
        LuaState ctx(state);

        u32 eventID = ctx.Get(0u, 1);

        LuaGameEvent gameEventID = static_cast<LuaGameEvent>(eventID);
        if (gameEventID == LuaGameEvent::Invalid || gameEventID >= LuaGameEvent::Count)
        {
            return 0;
        }

        if (!lua_isfunction(ctx.RawState(), 2))
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

        LuaState ctx(state);

        std::vector<i32>& funcRefList = _eventToLuaStateFuncRefList[id][key];
        for (i32 funcHandle : funcRefList)
        {
            ctx.GetRawI(LUA_REGISTRYINDEX, funcHandle);
            ctx.Push(id);
            ctx.Push(eventData->motd.c_str());
            ctx.PCall(2);
        }

        return 0;
    }

    i32 GameEventHandler::OnGameUpdated(lua_State* state, u32 eventID, LuaEventData* data)
    {
        LuaManager* luaManager = ServiceLocator::GetLuaManager();
        auto eventData = reinterpret_cast<LuaGameEventUpdatedData*>(data);

        u32 id = eventID;
        u64 key = reinterpret_cast<u64>(state);

        LuaState ctx(state);

        std::vector<i32>& funcRefList = _eventToLuaStateFuncRefList[id][key];
        for (i32 funcHandle : funcRefList)
        {
            ctx.GetRawI(LUA_REGISTRYINDEX, funcHandle);
            ctx.Push(id);
            ctx.Push(eventData->deltaTime);
            ctx.PCall(2);
        }

        return 0;
    }

    void GameEventHandler::CreateGameEventTable(lua_State* state)
    {
        LuaState ctx(state);

        ctx.CreateTableAndPopulate("GameEvent", [&]()
        {
            ctx.SetTable("Invalid", 0u);
            ctx.SetTable("Loaded",  1u);
            ctx.SetTable("Updated", 2u);
            ctx.SetTable("Count",   3u);
        });
    }
}
