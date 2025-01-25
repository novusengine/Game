#pragma once
#include "Game-Lib/Scripting/LuaDefines.h"

#include <Base/Types.h>

namespace Scripting::Database
{
    struct Item
    {
    public:
        static void Register(lua_State* state);

    };

    namespace ItemMethods
    {
        i32 GetItemInfo(lua_State* state);
    };
}