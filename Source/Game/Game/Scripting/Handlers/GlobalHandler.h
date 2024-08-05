#pragma once
#include "LuaHandlerBase.h"
#include "Game/Scripting/LuaDefines.h"
#include "Game/Scripting/LuaMethodTable.h"

#include <Base/Types.h>

namespace Scripting
{
    class GlobalHandler : public LuaHandlerBase
    {
    private:
        void Register(lua_State* state);
        void Clear() { }

    public: // Registered Functions
        static i32 AddCursor(lua_State* state);
        static i32 SetCursor(lua_State* state);
        static i32 GetCurrentMap(lua_State* state);
        static i32 LoadMap(lua_State* state);

        static i32 PanelCreate(lua_State* state);
        static i32 PanelGetPosition(lua_State* state);
        static i32 PanelGetExtents(lua_State* state);
        static i32 PanelToString(lua_State* state);
        static i32 PanelIndex(lua_State* state);
    };

    static LuaMethod globalMethods[] =
    {
        { "AddCursor",		GlobalHandler::AddCursor },
        { "SetCursor",		GlobalHandler::SetCursor },
        { "GetCurrentMap",	GlobalHandler::GetCurrentMap },
        { "LoadMap",		GlobalHandler::LoadMap },

        { nullptr, nullptr }
    };

    static LuaMethod panelMethods[] =
    {
        { "new",			GlobalHandler::PanelCreate },
        { "GetPosition",	GlobalHandler::PanelGetPosition },
        { "GetSize",		GlobalHandler::PanelGetExtents },

        { nullptr, nullptr }
    };

    static LuaMethod panelMetaTableMethods[] =
    {
        { "__tostring",	GlobalHandler::PanelToString },
        { "__index",	GlobalHandler::PanelIndex },

        { nullptr, nullptr }
    };
}