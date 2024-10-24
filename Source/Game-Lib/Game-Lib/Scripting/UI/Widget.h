#pragma once
#include "Game-Lib/Scripting/LuaDefines.h"
#include "Game-Lib/Scripting/LuaMethodTable.h"

#include <Base/Types.h>

#include <entt/fwd.hpp>

namespace Scripting::UI
{
    enum class WidgetType : u8
    {
        Canvas,
        Panel,
        Text,
        Widget
    };

    struct Widget
    {
    public:
        static void Register(lua_State* state);

    public:
        WidgetType type;
        std::string metaTableName;

        entt::entity entity;
    };

    namespace WidgetCreationMethods
    {
        i32 CreatePanel(lua_State* state);
        i32 CreateText(lua_State* state);
        i32 CreateWidget(lua_State* state);
    };

    static LuaMethod widgetCreationMethods[] =
    {
        { "NewPanel", WidgetCreationMethods::CreatePanel },
        { "NewText", WidgetCreationMethods::CreateText },
        { "NewWidget", WidgetCreationMethods::CreateWidget },

        { nullptr, nullptr }
    };

    namespace WidgetMethods
    {
        i32 SetEnabled(lua_State* state);
        i32 SetVisible(lua_State* state);
        i32 SetInteractable(lua_State* state);
        i32 SetFocusable(lua_State* state);

        i32 GetAnchor(lua_State* state);
        i32 GetRelativePoint(lua_State* state);

        i32 SetAnchor(lua_State* state);
        i32 SetRelativePoint(lua_State* state);

        i32 GetPos(lua_State* state);
        i32 GetPosX(lua_State* state);
        i32 GetPosY(lua_State* state);

        i32 SetPos(lua_State* state);
        i32 SetPosX(lua_State* state);
        i32 SetPosY(lua_State* state);

        i32 GetWorldPos(lua_State* state);
        i32 GetWorldPosX(lua_State* state);
        i32 GetWorldPosY(lua_State* state);

        i32 SetWorldPos(lua_State* state);
        i32 SetWorldPosX(lua_State* state);
        i32 SetWorldPosY(lua_State* state);
    }

    static LuaMethod widgetMethods[] =
    {
        { "SetEnabled", WidgetMethods::SetEnabled },
        { "SetVisible", WidgetMethods::SetVisible },
        { "SetInteractable", WidgetMethods::SetInteractable },
        { "SetFocusable", WidgetMethods::SetFocusable },

        { "GetAnchor", WidgetMethods::GetAnchor },
        { "GetRelativePoint", WidgetMethods::GetRelativePoint },

        { "SetAnchor", WidgetMethods::SetAnchor },
        { "SetRelativePoint", WidgetMethods::SetRelativePoint },

        { "GetPos", WidgetMethods::GetPos },
        { "GetPosX", WidgetMethods::GetPosX },
        { "GetPosY", WidgetMethods::GetPosY },

        { "SetPos", WidgetMethods::SetPos },
        { "SetPosX", WidgetMethods::SetPosX },
        { "SetPosY", WidgetMethods::SetPosY },

        { "GetWorldPos", WidgetMethods::GetWorldPos },
        { "GetWorldPosX", WidgetMethods::GetWorldPosX },
        { "GetWorldPosY", WidgetMethods::GetWorldPosY },

        { "SetWorldPos", WidgetMethods::SetWorldPos },
        { "SetWorldPosX", WidgetMethods::SetWorldPosX },
        { "SetWorldPosY", WidgetMethods::SetWorldPosY },

        { nullptr, nullptr }
    };

    namespace WidgetInputMethods
    {
        i32 SetOnMouseDown(lua_State* state);
        i32 SetOnMouseUp(lua_State* state);
        i32 SetOnMouseHeld(lua_State* state);

        i32 SetOnHoverBegin(lua_State* state);
        i32 SetOnHoverEnd(lua_State* state);
        i32 SetOnHoverHeld(lua_State* state);

        i32 SetOnFocusBegin(lua_State* state);
        i32 SetOnFocusEnd(lua_State* state);
        i32 SetOnFocusHeld(lua_State* state);

        i32 SetOnKeyboard(lua_State* state);
    }

    static LuaMethod widgetInputMethods[] =
    {
        { "SetOnMouseDown", WidgetInputMethods::SetOnMouseDown },
        { "SetOnMouseUp", WidgetInputMethods::SetOnMouseUp },
        { "SetOnMouseHeld", WidgetInputMethods::SetOnMouseHeld },

        { "SetOnHoverBegin", WidgetInputMethods::SetOnHoverBegin },
        { "SetOnHoverEnd", WidgetInputMethods::SetOnHoverEnd },
        { "SetOnHoverHeld", WidgetInputMethods::SetOnHoverHeld },

        { "SetOnFocusBegin", WidgetInputMethods::SetOnFocusBegin },
        { "SetOnFocusEnd", WidgetInputMethods::SetOnFocusEnd },
        { "SetOnFocusHeld", WidgetInputMethods::SetOnFocusHeld },

        { "SetOnKeyboard", WidgetInputMethods::SetOnKeyboard },

        { nullptr, nullptr }
    };
}