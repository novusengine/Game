#pragma once
#include "Game/Scripting/UI/Widget.h"

#include <Base/Types.h>

namespace Scripting::UI
{
    struct Button : public Widget
    {
    public:
        static void Register(lua_State* state);

    };

    namespace ButtonMethods
    {
        i32 SetText(lua_State* state);
    };
}