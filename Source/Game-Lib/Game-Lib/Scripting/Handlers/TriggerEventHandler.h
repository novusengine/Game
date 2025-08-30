#pragma once
#include "LuaEventHandlerBase.h"
#include "Game-Lib/Scripting/LuaDefines.h"
#include "Game-Lib/Scripting/LuaMethodTable.h"

#include <Base/Types.h>
#include <Meta/Generated/Game/LuaEnum.h>

namespace Scripting
{
    class TriggerEventHandler : public LuaEventHandlerBase
    {
    public:
        TriggerEventHandler() : LuaEventHandlerBase(static_cast<u32>(Generated::LuaTriggerEventEnum::Count)) { }

    public:
        void SetEventHandler(u32 eventID, EventHandlerFn fn);
        void CallEvent(lua_State* state, u32 eventID, LuaEventData* data);
        void RegisterEventCallback(lua_State* state, u32 eventID, i32 funcHandle);

    public: // Registered Functions
        static i32 RegisterTriggerEvent(lua_State* state);

    private:
        void Register(lua_State* state);

    private: // Event Handlers (Called by CallEvent)
        i32 OnTriggerEnter(lua_State* state, u32 eventID, LuaEventData* data);
        i32 OnTriggerExit(lua_State* state, u32 eventID, LuaEventData* data);
        i32 OnTriggerStay(lua_State* state, u32 eventID, LuaEventData* data);

    private: // Utility Functions
        void CreateTriggerEventTable(lua_State* state);
    };

    static LuaMethod triggerEventMethods[] =
    {
        { "RegisterTriggerEvent", TriggerEventHandler::RegisterTriggerEvent }
    };
}