#include "PlayerEventHandler.h"
#include "Game-Lib/Scripting/LuaState.h"
#include "Game-Lib/Scripting/LuaManager.h"
#include "Game-Lib/Scripting/Systems/LuaSystemBase.h"
#include "Game-Lib/Util/ServiceLocator.h"

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
            SetEventHandler(static_cast<u32>(LuaPlayerEvent::ContainerCreate), std::bind(&PlayerEventHandler::OnContainerCreate, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
            SetEventHandler(static_cast<u32>(LuaPlayerEvent::ContainerAddToSlot), std::bind(&PlayerEventHandler::OnContainerAddToSlot, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
            SetEventHandler(static_cast<u32>(LuaPlayerEvent::ContainerRemoveFromSlot), std::bind(&PlayerEventHandler::OnContainerRemoveFromSlot, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
            SetEventHandler(static_cast<u32>(LuaPlayerEvent::ContainerSwapSlots), std::bind(&PlayerEventHandler::OnContainerSwapSlots, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
        }

        CreatePlayerEventTable(state);
    }

    void PlayerEventHandler::SetEventHandler(u32 eventID, EventHandlerFn fn)
    {
        LuaPlayerEvent playerEventID = static_cast<LuaPlayerEvent>(eventID);

        if (playerEventID == LuaPlayerEvent::Invalid || playerEventID >= LuaPlayerEvent::Count)
        {
            return;
        }

        _eventToFuncHandlerList[eventID] = fn;
    }
    void PlayerEventHandler::CallEvent(lua_State* state, u32 eventID, LuaEventData* data)
    {
        if (eventID >= _eventToFuncHandlerList.size())
            return;

        LuaPlayerEvent playerEventID = static_cast<LuaPlayerEvent>(eventID);

        if (playerEventID == LuaPlayerEvent::Invalid || playerEventID >= LuaPlayerEvent::Count)
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

        LuaPlayerEvent eventID = static_cast<LuaPlayerEvent>(eventIDFromLua);
        if (eventID == LuaPlayerEvent::Invalid || eventID >= LuaPlayerEvent::Count)
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

            ctx.CreateTableAndPopulate([&]()
            {
                u32 numItems = static_cast<u32>(eventData->items.size());

                for (u32 i = 0; i < numItems; i++)
                {
                    const auto& eventItemData = eventData->items[i];
                    ctx.CreateTableAndPopulate([&ctx, &eventItemData]()
                    {
                        ctx.SetTable("slot", eventItemData.slot);
                        ctx.SetTable("itemID", eventItemData.itemID);
                        ctx.SetTable("count", eventItemData.count);
                    });

                    ctx.SetTable(i + 1);
                }
            });

            ctx.PCall(5);
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

        ctx.CreateTableAndPopulate("PlayerEvent", [&]()
        {
            ctx.SetTable("Invalid", 0u);
            ctx.SetTable("ContainerCreate", 1u);
            ctx.SetTable("ContainerAddToSlot", 2u);
            ctx.SetTable("ContainerRemoveFromSlot", 3u);
            ctx.SetTable("ContainerSwapSlots", 4u);
            ctx.SetTable("Count", 5u);
        });
    }
}
