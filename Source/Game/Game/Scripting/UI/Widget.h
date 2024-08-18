#pragma once
#include "Game/Scripting/LuaDefines.h"
#include "Game/Scripting/LuaMethodTable.h"

#include <Base/Types.h>

#include <entt/fwd.hpp>

namespace Scripting::UI
{
    enum class WidgetType : u8
    {
        Button,
        Canvas,
        Panel,
        Text
    };

    struct Widget
    {
    public:
        WidgetType type;

        entt::entity entity;
    };

    namespace WidgetCreationMethods
    {
        i32 CreateButton(lua_State* state);
        i32 CreatePanel(lua_State* state);
        i32 CreateText(lua_State* state);
    };

    static LuaMethod widgetCreationMethods[] =
    {
        { "NewButton", WidgetCreationMethods::CreateButton },
        { "NewPanel", WidgetCreationMethods::CreatePanel },
        { "NewText", WidgetCreationMethods::CreateText },

        { nullptr, nullptr }
    };

    namespace WidgetMethods
    {
        i32 SetAnchor(lua_State* state);
        i32 SetRelativePoint(lua_State* state);
    }

    static LuaMethod widgetMethods[] =
    {
        { "SetAnchor", WidgetMethods::SetAnchor },
        { "SetRelativePoint", WidgetMethods::SetRelativePoint },

        { nullptr, nullptr }
    };
}