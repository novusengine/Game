#pragma once
#include "LuaEventHandlerBase.h"
#include "Game/Scripting/LuaDefines.h"
#include "Game/Scripting/LuaMethodTable.h"

#include <Base/Types.h>

namespace Scripting
{
    class GameEventHandler : public LuaEventHandlerBase
    {
    public:
        GameEventHandler() : LuaEventHandlerBase(static_cast<u32>(LuaGameEvent::Count)) { }

    public:
        void SetEventHandler(u32 eventID, EventHandlerFn fn);
        void CallEvent(lua_State* state, u32 eventID, LuaEventData* data);
        void RegisterEventCallback(lua_State* state, u32 eventID, i32 funcHandle);

    public: // Registered Functions
        static i32 RegisterGameEvent(lua_State* state);

    private:
        void Register(lua_State* state);

    private: // Event Handlers (Called by CallEvent)
        i32 OnGameLoaded(lua_State* state, u32 eventID, LuaEventData* data);
        i32 OnGameUpdated(lua_State* state, u32 eventID, LuaEventData* data);

    private: // Utility Functions
        void CreateGameEventTable(lua_State* state);
    };

    static LuaMethod gameEventMethods[] =
    {
        { "RegisterGameEvent", GameEventHandler::RegisterGameEvent },

        { nullptr, nullptr }
    };
}