#pragma once
#include "Game-Lib/Scripting/UI/Widget.h"

#include <Base/Types.h>

namespace Scripting::UI
{
    struct Text : public Widget
    {
    public:
        static void Register(lua_State* state);

    };

    namespace TextMethods
    {
        i32 SetText(lua_State* state);
        i32 GetSize(lua_State* state);
        i32 SetColor(lua_State* state);
        i32 SetWrapWidth(lua_State* state);
    };

    static LuaMethod textMethods[] =
    {
        { "SetText", TextMethods::SetText },
        { "GetSize", TextMethods::GetSize },
        { "SetColor", TextMethods::SetColor },
        { "SetWrapWidth", TextMethods::SetWrapWidth }
    };
}