#include "Text.h"

#include "Game/Scripting/LuaState.h"
#include "Game/Scripting/UI/Canvas.h"
#include "Game/Scripting/UI/Panel.h"

namespace Scripting::UI
{
    static LuaMethod textMethods[] =
    {
        //{ "NewPanel", PanelMethods::CreatePanel },
        //{ "NewText", PanelMethods::CreateText },

        { nullptr, nullptr }
    };

    void Text::Register(lua_State* state)
    {
        LuaMetaTable<Text>::Register(state, "TextMetaTable");
        LuaMetaTable<Text>::Set(state, textMethods);
        LuaMetaTable<Text>::Set(state, widgetMethods);
    }

    namespace TextMethods
    {
    }
}