#include "Button.h"

#include "Game/Scripting/LuaState.h"
#include "Game/Scripting/UI/Canvas.h"
#include "Game/Scripting/UI/Text.h"

namespace Scripting::UI
{
    static LuaMethod buttonMethods[] =
    {
        { "SetText", ButtonMethods::SetText },

        { nullptr, nullptr }
    };

    void Button::Register(lua_State* state)
    {
        LuaMetaTable<Button>::Register(state, "ButtonMetaTable");

        LuaMetaTable<Button>::Set(state, buttonMethods);
        LuaMetaTable<Button>::Set(state, widgetCreationMethods);
        LuaMetaTable<Button>::Set(state, widgetMethods);
    }

    namespace ButtonMethods
    {
        i32 SetText(lua_State* state)
        {
            return 0;
        }
    }
}