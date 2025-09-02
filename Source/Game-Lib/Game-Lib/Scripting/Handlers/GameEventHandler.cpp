#include "GameEventHandler.h"
#include "Game-Lib/Scripting/LuaState.h"
#include "Game-Lib/Scripting/LuaManager.h"
#include "Game-Lib/Scripting/Systems/LuaSystemBase.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Base/CVarSystem/CVarSystem.h>

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
            SetEventHandler(static_cast<u32>(Generated::LuaGameEventEnum::Loaded), std::bind(&GameEventHandler::OnGameLoaded, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
            SetEventHandler(static_cast<u32>(Generated::LuaGameEventEnum::Updated), std::bind(&GameEventHandler::OnGameUpdated, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
            SetEventHandler(static_cast<u32>(Generated::LuaGameEventEnum::MapLoading), std::bind(&GameEventHandler::OnMapLoading, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
            SetEventHandler(static_cast<u32>(Generated::LuaGameEventEnum::ChatMessageReceived), std::bind(&GameEventHandler::OnChatMessageReceived, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
        }

        CreateGameEventTable(state);
    }

    void GameEventHandler::PostLoad(lua_State* state)
    {
        const char* motd = CVarSystem::Get()->GetStringCVar(CVarCategory::Client, "scriptingMotd");

        LuaGameEventLoadedData eventData =
        {
            .motd = motd
        };

        auto* luaManager = ServiceLocator::GetLuaManager();
        auto gameEventHandler = luaManager->GetLuaHandler<GameEventHandler*>(LuaHandlerType::GameEvent);
        gameEventHandler->CallEvent(state, static_cast<u32>(Generated::LuaGameEventEnum::Loaded), &eventData);
    }

    void GameEventHandler::SetEventHandler(u32 eventID, EventHandlerFn fn)
    {
        Generated::LuaGameEventEnum gameEventID = static_cast<Generated::LuaGameEventEnum>(eventID);

        if (gameEventID == Generated::LuaGameEventEnum::Invalid || gameEventID >= Generated::LuaGameEventEnum::Count)
        {
            return;
        }

        _eventToFuncHandlerList[eventID] = fn;
    }
    void GameEventHandler::CallEvent(lua_State* state, u32 eventID, LuaEventData* data)
    {
        if (eventID >= _eventToFuncHandlerList.size())
            return;

        Generated::LuaGameEventEnum gameEventID = static_cast<Generated::LuaGameEventEnum>(eventID);

        if (gameEventID == Generated::LuaGameEventEnum::Invalid || gameEventID >= Generated::LuaGameEventEnum::Count)
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

        Generated::LuaGameEventEnum gameEventID = static_cast<Generated::LuaGameEventEnum>(eventID);
        if (gameEventID == Generated::LuaGameEventEnum::Invalid || gameEventID >= Generated::LuaGameEventEnum::Count)
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

    i32 GameEventHandler::OnMapLoading(lua_State* state, u32 eventID, LuaEventData* data)
    {
        LuaManager* luaManager = ServiceLocator::GetLuaManager();
        auto eventData = reinterpret_cast<LuaGameEventMapLoadingData*>(data);

        u32 id = eventID;
        u64 key = reinterpret_cast<u64>(state);

        LuaState ctx(state);

        std::vector<i32>& funcRefList = _eventToLuaStateFuncRefList[id][key];
        for (i32 funcHandle : funcRefList)
        {
            ctx.GetRawI(LUA_REGISTRYINDEX, funcHandle);
            ctx.Push(id);
            ctx.Push(eventData->mapInternalName);
            ctx.PCall(2);
        }

        return 0;
    }

    i32 GameEventHandler::OnChatMessageReceived(lua_State* state, u32 eventID, LuaEventData* data)
    {
        LuaManager* luaManager = ServiceLocator::GetLuaManager();
        auto eventData = reinterpret_cast<LuaGameEventChatMessageReceivedData*>(data);

        u32 id = eventID;
        u64 key = reinterpret_cast<u64>(state);

        LuaState ctx(state);

        std::vector<i32>& funcRefList = _eventToLuaStateFuncRefList[id][key];
        for (i32 funcHandle : funcRefList)
        {
            ctx.GetRawI(LUA_REGISTRYINDEX, funcHandle);
            ctx.Push(id);
            ctx.Push(eventData->sender);
            ctx.Push(eventData->channel);
            ctx.Push(eventData->message);
            ctx.PCall(4);
        }

        return 0;
    }

    void GameEventHandler::CreateGameEventTable(lua_State* state)
    {
        LuaState ctx(state);

        ctx.CreateTableAndPopulate(Generated::LuaGameEventEnumMeta::EnumName.data(), [&]()
        {
            for (const auto& pair : Generated::LuaGameEventEnumMeta::EnumList)
            {
                ctx.SetTable(pair.first.data(), pair.second);
            }
        });
    }
}
