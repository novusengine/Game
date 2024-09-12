#include "Panel.h"

#include "Game-Lib/Scripting/LuaState.h"
#include "Game-Lib/Scripting/UI/Canvas.h"
#include "Game-Lib/Scripting/UI/Text.h"

namespace Scripting::UI
{
    static LuaMethod panelMethods[] =
    {

        { nullptr, nullptr }
    };

    void Panel::Register(lua_State* state)
    {
        LuaMetaTable<Panel>::Register(state, "PanelMetaTable");

        LuaMetaTable<Panel>::Set(state, widgetCreationMethods);
        LuaMetaTable<Panel>::Set(state, widgetMethods);
        LuaMetaTable<Panel>::Set(state, widgetInputMethods);
        LuaMetaTable<Panel>::Set(state, panelMethods);
    }

    namespace PanelMethods
    {
        
    }
}