#pragma once
#include "Game-Lib/Scripting/LuaDefines.h"

#include <Base/Types.h>

namespace Scripting::Database
{
    struct Spell
    {
    public:
        static void Register(lua_State* state);

    };

    namespace SpellMethods
    {
        i32 GetSpellInfo(lua_State* state);
    };
}