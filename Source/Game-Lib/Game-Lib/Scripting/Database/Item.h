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
        i32 GetItemStatInfo(lua_State* state);
        i32 GetItemArmorInfo(lua_State* state);
        i32 GetItemWeaponInfo(lua_State* state);
        i32 GetItemShieldInfo(lua_State* state);
        i32 GetItemDisplayInfo(lua_State* state);
        i32 GetItemEffects(lua_State* state);

        i32 GetIconInfo(lua_State* state);
    };
}