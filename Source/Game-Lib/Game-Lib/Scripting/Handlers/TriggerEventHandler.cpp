#include "TriggerEventHandler.h"
#include "Game-Lib/Scripting/LuaState.h"
#include "Game-Lib/Scripting/LuaManager.h"
#include "Game-Lib/Scripting/Systems/LuaSystemBase.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <lualib.h>

namespace Scripting
{
    void TriggerEventHandler::Register(lua_State* state)
    {
        // Register Functions
        {
            LuaMethodTable::Set(state, triggerEventMethods);
        }

        // Set Event Handlers
        {
            SetEventHandler(static_cast<u32>(Generated::LuaTriggerEventEnum::OnEnter), std::bind(&TriggerEventHandler::OnTriggerEnter, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
            SetEventHandler(static_cast<u32>(Generated::LuaTriggerEventEnum::OnExit), std::bind(&TriggerEventHandler::OnTriggerExit, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
            SetEventHandler(static_cast<u32>(Generated::LuaTriggerEventEnum::OnStay), std::bind(&TriggerEventHandler::OnTriggerStay, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
        }

        CreateTriggerEventTable(state);
    }

    void TriggerEventHandler::SetEventHandler(u32 eventID, EventHandlerFn fn)
    {
        Generated::LuaTriggerEventEnum triggerEventID = static_cast<Generated::LuaTriggerEventEnum>(eventID);

        if (triggerEventID == Generated::LuaTriggerEventEnum::Invalid || triggerEventID >= Generated::LuaTriggerEventEnum::Count)
        {
            return;
        }

        _eventToFuncHandlerList[eventID] = fn;
    }
    void TriggerEventHandler::CallEvent(lua_State* state, u32 eventID, LuaEventData* data)
    {
        if (eventID >= _eventToFuncHandlerList.size())
            return;

        Generated::LuaTriggerEventEnum triggerEventID = static_cast<Generated::LuaTriggerEventEnum>(eventID);

        if (triggerEventID == Generated::LuaTriggerEventEnum::Invalid || triggerEventID >= Generated::LuaTriggerEventEnum::Count)
        {
            return;
        }

        EventHandlerFn& fn = _eventToFuncHandlerList[eventID];
        if (fn)
            fn(state, eventID, data);
    }
    void TriggerEventHandler::RegisterEventCallback(lua_State* state, u32 eventID, i32 funcHandle)
    {
        LuaManager* luaManager = ServiceLocator::GetLuaManager();

        u32 id = eventID;
        u64 key = reinterpret_cast<u64>(state);

        std::vector<i32>& funcRefList = _eventToLuaStateFuncRefList[id][key];
        funcRefList.push_back(funcHandle);
    }
    
    i32 TriggerEventHandler::RegisterTriggerEvent(lua_State* state)
    {
        LuaState ctx(state);

        u32 eventIDFromLua = ctx.Get(0u, 1);

        Generated::LuaTriggerEventEnum triggerEventID = static_cast<Generated::LuaTriggerEventEnum>(eventIDFromLua);
        if (triggerEventID == Generated::LuaTriggerEventEnum::Invalid || triggerEventID >= Generated::LuaTriggerEventEnum::Count)
        {
            return 0;
        }

        if (!lua_isfunction(ctx.RawState(), 2))
        {
            return 0;
        }

        i32 funcHandle = ctx.GetRef(2);

        LuaManager* luaManager = ServiceLocator::GetLuaManager();
        auto eventHandler = luaManager->GetLuaHandler<TriggerEventHandler*>(LuaHandlerType::TriggerEvent);
        eventHandler->RegisterEventCallback(state, eventIDFromLua, funcHandle);

        return 0;
    }

    i32 TriggerEventHandler::OnTriggerEnter(lua_State* state, u32 eventID, LuaEventData* data)
    {
        LuaManager* luaManager = ServiceLocator::GetLuaManager();
        auto eventData = reinterpret_cast<LuaTriggerEventOnTriggerEnterData*>(data);
    
        u32 id = eventID;
        u64 key = reinterpret_cast<u64>(state);
    
        LuaState ctx(state);
    
        std::vector<i32>& funcRefList = _eventToLuaStateFuncRefList[id][key];
        for (i32 funcHandle : funcRefList)
        {
            ctx.GetRawI(LUA_REGISTRYINDEX, funcHandle);
            ctx.Push(id);
            ctx.Push(eventData->triggerID);
            ctx.Push(eventData->playerID);

            ctx.PCall(3);
        }
    
        return 0;
    }

    i32 TriggerEventHandler::OnTriggerExit(lua_State* state, u32 eventID, LuaEventData* data)
    {
        LuaManager* luaManager = ServiceLocator::GetLuaManager();
        auto eventData = reinterpret_cast<LuaTriggerEventOnTriggerExitData*>(data);

        u32 id = eventID;
        u64 key = reinterpret_cast<u64>(state);

        LuaState ctx(state);

        std::vector<i32>& funcRefList = _eventToLuaStateFuncRefList[id][key];
        for (i32 funcHandle : funcRefList)
        {
            ctx.GetRawI(LUA_REGISTRYINDEX, funcHandle);
            ctx.Push(id);
            ctx.Push(eventData->triggerID);
            ctx.Push(eventData->playerID);

            ctx.PCall(3);
        }

        return 0;
    }

    i32 TriggerEventHandler::OnTriggerStay(lua_State* state, u32 eventID, LuaEventData* data)
    {
        LuaManager* luaManager = ServiceLocator::GetLuaManager();
        auto eventData = reinterpret_cast<LuaTriggerEventOnTriggerStayData*>(data);

        u32 id = eventID;
        u64 key = reinterpret_cast<u64>(state);

        LuaState ctx(state);

        std::vector<i32>& funcRefList = _eventToLuaStateFuncRefList[id][key];
        for (i32 funcHandle : funcRefList)
        {
            ctx.GetRawI(LUA_REGISTRYINDEX, funcHandle);
            ctx.Push(id);
            ctx.Push(eventData->triggerID);
            ctx.Push(eventData->playerID);

            ctx.PCall(3);
        }

        return 0;
    }

    void TriggerEventHandler::CreateTriggerEventTable(lua_State* state)
    {
        LuaState ctx(state);

        ctx.CreateTableAndPopulate(Generated::LuaTriggerEventEnumMeta::EnumName.data(), [&]()
        {
            for (const auto& pair : Generated::LuaTriggerEventEnumMeta::EnumList)
            {
                ctx.SetTable(pair.first.data(), pair.second);
            }
        });
    }
}
