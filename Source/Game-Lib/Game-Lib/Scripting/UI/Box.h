#pragma once
#include "Game/Scripting/UI/Widget.h"

#include <Base/Types.h>

namespace Scripting::UI
{
    struct Box
    {
    public:
        static void Register(lua_State* state);

    };

    namespace BoxMethods
    {
        i32 CreateBox(lua_State* state);
    };
}