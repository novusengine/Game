#pragma once
#include "LuaEventHandlerBase.h"
#include "Game-Lib/Scripting/LuaDefines.h"
#include "Game-Lib/Scripting/LuaMethodTable.h"

#include <Meta/Generated/Game/LuaEnum.h>

#include <Base/Types.h>

namespace Scripting
{
    class PlayerEventHandler : public LuaEventHandlerBase
    {
    public:
        PlayerEventHandler() : LuaEventHandlerBase(static_cast<u32>(Generated::LuaPlayerEventEnum::Count)) { }

    public:
        void SetEventHandler(u32 eventID, EventHandlerFn fn);
        void CallEvent(lua_State* state, u32 eventID, LuaEventData* data);
        void RegisterEventCallback(lua_State* state, u32 eventID, i32 funcHandle);

    public: // Registered Functions
        static i32 RegisterPlayerEvent(lua_State* state);

    private:
        void Register(lua_State* state);

    private: // Event Handlers (Called by CallEvent)
        i32 OnUnitCreated(lua_State* state, u32 eventID, LuaEventData* data);
        i32 OnUnitDestroyed(lua_State* state, u32 eventID, LuaEventData* data);
        i32 OnContainerCreate(lua_State* state, u32 eventID, LuaEventData* data);
        i32 OnContainerAddToSlot(lua_State* state, u32 eventID, LuaEventData* data);
        i32 OnContainerRemoveFromSlot(lua_State* state, u32 eventID, LuaEventData* data);
        i32 OnContainerSwapSlots(lua_State* state, u32 eventID, LuaEventData* data);

    private: // Utility Functions
        void CreatePlayerEventTable(lua_State* state);
    };

    static LuaMethod playerEventMethods[] =
    {
        { "RegisterPlayerEvent", PlayerEventHandler::RegisterPlayerEvent }
    };
}