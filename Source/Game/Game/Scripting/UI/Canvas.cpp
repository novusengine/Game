#include "Canvas.h"

#include "Game/Scripting/LuaState.h"
#include "Game/Scripting/UI/Panel.h"
#include "Game/Scripting/UI/Text.h"

namespace Scripting::UI
{
    static LuaMethod canvasMethods[] =
    {
        //{ "NewPanel", PanelMethods::CreatePanel },
        //{ "NewText", TextMethods::CreateText },

        { nullptr, nullptr }
    };

    void Canvas::Register(lua_State* state)
    {
        LuaMetaTable<Canvas>::Register(state, "CanvasMetaTable");

        LuaMetaTable<Canvas>::Set(state, canvasMethods);
        LuaMetaTable<Canvas>::Set(state, widgetCreationMethods);
    }

    namespace CanvasMethods
    {
        
    }
}