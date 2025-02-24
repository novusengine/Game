#include "Canvas.h"

#include "Game-Lib/Scripting/LuaState.h"
#include "Game-Lib/Scripting/UI/Panel.h"
#include "Game-Lib/Scripting/UI/Text.h"

namespace Scripting::UI
{
    //static LuaMethod canvasMethods[] =
    //{
    //    //{ "NewPanel", PanelMethods::CreatePanel },
    //    //{ "NewText", TextMethods::CreateText },
    //};

    void Canvas::Register(lua_State* state)
    {
        LuaMetaTable<Canvas>::Register(state, "CanvasMetaTable");

        LuaMetaTable<Canvas>::Set(state, widgetCreationMethods);
        //LuaMetaTable<Canvas>::Set(state, canvasMethods);
    }

    namespace CanvasMethods
    {
        
    }
}