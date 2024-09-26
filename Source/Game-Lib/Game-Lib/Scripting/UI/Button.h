#pragma once
#include "Game-Lib/Scripting/UI/Widget.h"

#include <Base/Types.h>

#include <entt/entt.hpp>

namespace Scripting::UI
{
    struct Button : public Widget
    {
    public:
        static void Register(lua_State* state);

        Widget panelWidget;
        Widget textWidget;
    };

    namespace ButtonMethods
    {
        i32 SetText(lua_State* state);

        i32 GetPanelWidget(lua_State* state);
        i32 GetTextWidget(lua_State* state);
    };
}