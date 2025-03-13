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
        i32 GetText(lua_State* state);
        i32 SetText(lua_State* state);
        i32 GetRawText(lua_State* state);
        i32 GetSize(lua_State* state);
        i32 GetColor(lua_State* state);
        i32 SetColor(lua_State* state);
        i32 GetWrapWidth(lua_State* state);
        i32 SetWrapWidth(lua_State* state);
    };

    static LuaMethod textMethods[] =
    {
        { "GetText", TextMethods::GetText },
        { "SetText", TextMethods::SetText },
        { "GetRawText", TextMethods::GetRawText },
        { "GetSize", TextMethods::GetSize },
        { "GetColor", TextMethods::GetColor },
        { "SetColor", TextMethods::SetColor },
        { "GetWrapWidth", TextMethods::GetWrapWidth },
        { "SetWrapWidth", TextMethods::SetWrapWidth }
    };
}