#pragma once
#include "Game-Lib/Scripting/UI/Widget.h"

#include <Base/Types.h>

namespace Scripting::UI
{
    struct Panel : public Widget
    {
    public:
        static void Register(lua_State* state);

    };

    namespace PanelMethods
    {
        i32 GetSize(lua_State* state);
        i32 GetWidth(lua_State* state);
        i32 GetHeight(lua_State* state);
        i32 SetSize(lua_State* state);
        i32 SetWidth(lua_State* state);
        i32 SetHeight(lua_State* state);
        i32 SetBackground(lua_State* state);
        i32 SetForeground(lua_State* state);
    };
}