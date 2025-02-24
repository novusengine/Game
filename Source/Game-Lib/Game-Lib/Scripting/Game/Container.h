#pragma once
#include "Game-Lib/Scripting/LuaDefines.h"

#include <Base/Types.h>

namespace Scripting::Game
{
    struct Container
    {
    public:
        static void Register(lua_State* state);

    };

    namespace ContainerMethods
    {
        i32 RequestSwapSlots(lua_State* state);
        i32 GetContainerItems(lua_State* state);
    };
}