#pragma once
#include "LuaHandlerBase.h"
#include "Game-Lib/Scripting/LuaDefines.h"
#include "Game-Lib/Scripting/LuaMethodTable.h"

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
        static i32 EquipItem(lua_State* state);
        static i32 UnEquipItem(lua_State* state);
        static i32 GetEquippedItem(lua_State* state);
        static i32 ExecCmd(lua_State* state);
    };

    static LuaMethod globalMethods[] =
    {
        { "AddCursor",		    GlobalHandler::AddCursor },
        { "SetCursor",		    GlobalHandler::SetCursor },
        { "GetCurrentMap",	    GlobalHandler::GetCurrentMap },
        { "LoadMap",		    GlobalHandler::LoadMap },
        { "EquipItem",		    GlobalHandler::EquipItem },
        { "UnEquipItem",	    GlobalHandler::UnEquipItem },
        { "GetEquippedItem",    GlobalHandler::GetEquippedItem },
        { "ExecCmd",            GlobalHandler::ExecCmd },
    };
}