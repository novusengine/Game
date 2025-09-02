#include "PlayerEventHandler.h"
#include "Game-Lib/Scripting/LuaState.h"
#include "Game-Lib/Scripting/LuaManager.h"
#include "Game-Lib/Scripting/Systems/LuaSystemBase.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <entt/entt.hpp>
#include <lualib.h>

namespace Scripting
{
    void PlayerEventHandler::Register(lua_State* state)
    {
        // Register Functions
        {
            LuaMethodTable::Set(state, playerEventMethods);
        }

        // Set Event Handlers
        {
            SetEventHandler(static_cast<u32>(Generated::LuaPlayerEventEnum::Created), std::bind(&PlayerEventHandler::OnUnitCreated, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
            SetEventHandler(static_cast<u32>(Generated::LuaPlayerEventEnum::Destroyed), std::bind(&PlayerEventHandler::OnUnitDestroyed, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
            SetEventHandler(static_cast<u32>(Generated::LuaPlayerEventEnum::ContainerCreate), std::bind(&PlayerEventHandler::OnContainerCreate, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
            SetEventHandler(static_cast<u32>(Generated::LuaPlayerEventEnum::ContainerAddToSlot), std::bind(&PlayerEventHandler::OnContainerAddToSlot, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
            SetEventHandler(static_cast<u32>(Generated::LuaPlayerEventEnum::ContainerRemoveFromSlot), std::bind(&PlayerEventHandler::OnContainerRemoveFromSlot, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
            SetEventHandler(static_cast<u32>(Generated::LuaPlayerEventEnum::ContainerSwapSlots), std::bind(&PlayerEventHandler::OnContainerSwapSlots, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
        }

        CreatePlayerEventTable(state);
    }

    void PlayerEventHandler::SetEventHandler(u32 eventID, EventHandlerFn fn)
    {
        Generated::LuaPlayerEventEnum playerEventID = static_cast<Generated::LuaPlayerEventEnum>(eventID);

        if (playerEventID == Generated::LuaPlayerEventEnum::Invalid || playerEventID >= Generated::LuaPlayerEventEnum::Count)
        {
            return;
        }

        _eventToFuncHandlerList[eventID] = fn;
    }
    void PlayerEventHandler::CallEvent(lua_State* state, u32 eventID, LuaEventData* data)
    {
        if (eventID >= _eventToFuncHandlerList.size())
            return;

        Generated::LuaPlayerEventEnum playerEventID = static_cast<Generated::LuaPlayerEventEnum>(eventID);

        if (playerEventID == Generated::LuaPlayerEventEnum::Invalid || playerEventID >= Generated::LuaPlayerEventEnum::Count)
        {
            return;
        }

        EventHandlerFn& fn = _eventToFuncHandlerList[eventID];
        if (fn)
            fn(state, eventID, data);
    }
    void PlayerEventHandler::RegisterEventCallback(lua_State* state, u32 eventID, i32 funcHandle)
    {
        LuaManager* luaManager = ServiceLocator::GetLuaManager();

        u32 id = eventID;
        u64 key = reinterpret_cast<u64>(state);

        std::vector<i32>& funcRefList = _eventToLuaStateFuncRefList[id][key];
        funcRefList.push_back(funcHandle);
    }
    
    i32 PlayerEventHandler::RegisterPlayerEvent(lua_State* state)
    {
        LuaState ctx(state);

        u32 eventIDFromLua = ctx.Get(0u, 1);

        Generated::LuaPlayerEventEnum eventID = static_cast<Generated::LuaPlayerEventEnum>(eventIDFromLua);
        if (eventID == Generated::LuaPlayerEventEnum::Invalid || eventID >= Generated::LuaPlayerEventEnum::Count)
        {
            return 0;
        }

        if (!lua_isfunction(ctx.RawState(), 2))
        {
            return 0;
        }

        i32 funcHandle = ctx.GetRef(2);

        LuaManager* luaManager = ServiceLocator::GetLuaManager();
        auto eventHandler = luaManager->GetLuaHandler<PlayerEventHandler*>(LuaHandlerType::PlayerEvent);
        eventHandler->RegisterEventCallback(state, eventIDFromLua, funcHandle);

        return 0;
    }

    i32 PlayerEventHandler::OnUnitCreated(lua_State* state, u32 eventID, LuaEventData* data)
    {
        LuaManager* luaManager = ServiceLocator::GetLuaManager();
        auto eventData = reinterpret_cast<LuaUnitEventCreatedData*>(data);

        u32 id = eventID;
        u64 key = reinterpret_cast<u64>(state);

        LuaState ctx(state);

        std::vector<i32>& funcRefList = _eventToLuaStateFuncRefList[id][key];
        for (i32 funcHandle : funcRefList)
        {
            ctx.GetRawI(LUA_REGISTRYINDEX, funcHandle);
            ctx.Push(id);
            ctx.Push(entt::to_integral(eventData->unitID));

            ctx.PCall(2);
        }

        return 0;
    }

    i32 PlayerEventHandler::OnUnitDestroyed(lua_State* state, u32 eventID, LuaEventData* data)
    {
        LuaManager* luaManager = ServiceLocator::GetLuaManager();
        auto eventData = reinterpret_cast<LuaUnitEventCreatedData*>(data);

        u32 id = eventID;
        u64 key = reinterpret_cast<u64>(state);

        LuaState ctx(state);

        std::vector<i32>& funcRefList = _eventToLuaStateFuncRefList[id][key];
        for (i32 funcHandle : funcRefList)
        {
            ctx.GetRawI(LUA_REGISTRYINDEX, funcHandle);
            ctx.Push(id);
            ctx.Push(entt::to_integral(eventData->unitID));

            ctx.PCall(2);
        }

        return 0;
    }

    i32 PlayerEventHandler::OnContainerCreate(lua_State* state, u32 eventID, LuaEventData* data)
    {
        LuaManager* luaManager = ServiceLocator::GetLuaManager();
        auto eventData = reinterpret_cast<LuaPlayerEventContainerCreateData*>(data);
    
        u32 id = eventID;
        u64 key = reinterpret_cast<u64>(state);
    
        LuaState ctx(state);
    
        std::vector<i32>& funcRefList = _eventToLuaStateFuncRefList[id][key];
        for (i32 funcHandle : funcRefList)
        {
            ctx.GetRawI(LUA_REGISTRYINDEX, funcHandle);
            ctx.Push(id);
            ctx.Push(eventData->index);
            ctx.Push(eventData->numSlots);
            ctx.Push(eventData->itemID);

            ctx.PCall(4);
        }
    
        return 0;
    }

    i32 PlayerEventHandler::OnContainerAddToSlot(lua_State* state, u32 eventID, LuaEventData* data)
    {
        LuaManager* luaManager = ServiceLocator::GetLuaManager();
        auto eventData = reinterpret_cast<LuaPlayerEventContainerAddToSlotData*>(data);

        u32 id = eventID;
        u64 key = reinterpret_cast<u64>(state);

        LuaState ctx(state);

        std::vector<i32>& funcRefList = _eventToLuaStateFuncRefList[id][key];
        for (i32 funcHandle : funcRefList)
        {
            ctx.GetRawI(LUA_REGISTRYINDEX, funcHandle);
            ctx.Push(id);
            ctx.Push(eventData->containerIndex);
            ctx.Push(eventData->slotIndex);
            ctx.Push(eventData->itemID);
            ctx.Push(eventData->count);

            ctx.PCall(5);
        }

        return 0;
    }

    i32 PlayerEventHandler::OnContainerRemoveFromSlot(lua_State* state, u32 eventID, LuaEventData* data)
    {
        LuaManager* luaManager = ServiceLocator::GetLuaManager();
        auto eventData = reinterpret_cast<LuaPlayerEventContainerRemoveFromSlotData*>(data);

        u32 id = eventID;
        u64 key = reinterpret_cast<u64>(state);

        LuaState ctx(state);

        std::vector<i32>& funcRefList = _eventToLuaStateFuncRefList[id][key];
        for (i32 funcHandle : funcRefList)
        {
            ctx.GetRawI(LUA_REGISTRYINDEX, funcHandle);
            ctx.Push(id);
            ctx.Push(eventData->containerIndex);
            ctx.Push(eventData->slotIndex);

            ctx.PCall(3);
        }

        return 0;
    }

    i32 PlayerEventHandler::OnContainerSwapSlots(lua_State* state, u32 eventID, LuaEventData* data)
    {
        LuaManager* luaManager = ServiceLocator::GetLuaManager();
        auto eventData = reinterpret_cast<LuaPlayerEventContainerSwapSlotsData*>(data);
    
        u32 id = eventID;
        u64 key = reinterpret_cast<u64>(state);
    
        LuaState ctx(state);
    
        std::vector<i32>& funcRefList = _eventToLuaStateFuncRefList[id][key];
        for (i32 funcHandle : funcRefList)
        {
            ctx.GetRawI(LUA_REGISTRYINDEX, funcHandle);
            ctx.Push(id);
            ctx.Push(eventData->srcContainerIndex);
            ctx.Push(eventData->destContainerIndex);
            ctx.Push(eventData->srcSlotIndex);
            ctx.Push(eventData->destSlotIndex);

            ctx.PCall(5);
        }
    
        return 0;
    }

    void PlayerEventHandler::CreatePlayerEventTable(lua_State* state)
    {
        LuaState ctx(state);

        ctx.CreateTableAndPopulate(Generated::LuaPlayerEventEnumMeta::EnumName.data(), [&]()
        {
            for (const auto& pair : Generated::LuaPlayerEventEnumMeta::EnumList)
            {
                ctx.SetTable(pair.first.data(), pair.second);
            }
        });
    }
}
