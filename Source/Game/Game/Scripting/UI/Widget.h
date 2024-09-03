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
        std::string metaTableName;

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
        i32 SetEnabled(lua_State* state);
        i32 SetVisible(lua_State* state);
        i32 SetInteractable(lua_State* state);
        i32 SetAnchor(lua_State* state);
        i32 SetRelativePoint(lua_State* state);
    }

    static LuaMethod widgetMethods[] =
    {
        { "SetEnabled", WidgetMethods::SetEnabled },
        { "SetVisible", WidgetMethods::SetVisible },
        { "SetInteractable", WidgetMethods::SetInteractable },
        { "SetAnchor", WidgetMethods::SetAnchor },
        { "SetRelativePoint", WidgetMethods::SetRelativePoint },

        { nullptr, nullptr }
    };
}