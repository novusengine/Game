#include "Panel.h"

#include "Game/Scripting/LuaState.h"
#include "Game/Scripting/UI/Canvas.h"
#include "Game/Scripting/UI/Text.h"

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
        LuaMetaTable<Panel>::Set(state, panelMethods);
    }

    namespace PanelMethods
    {
        
    }
}