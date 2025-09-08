#pragma once
#include <Base/Types.h>

#include <Scripting/Defines.h>
#include <Scripting/LuaMethodTable.h>

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
        static void Register(Zenith* zenith);

    public:
        WidgetType type;
        std::string metaTableName;

        entt::entity entity;
        entt::entity canvasEntity;
    };

    namespace WidgetCreationMethods
    {
        i32 CreatePanel(Zenith* zenith, Widget* parent);
        i32 CreateText(Zenith* zenith, Widget* parent);
        i32 CreateWidget(Zenith* zenith, Widget* parent);
    };

    static LuaRegister<Widget> widgetCreationMethods[] =
    {
        { "NewPanel", WidgetCreationMethods::CreatePanel },
        { "NewText", WidgetCreationMethods::CreateText },
        { "NewWidget", WidgetCreationMethods::CreateWidget }
    };

    namespace WidgetMethods
    {
        i32 SetEnabled(Zenith* zenith, Widget* widget);
        i32 SetVisible(Zenith* zenith, Widget* widget);
        i32 SetInteractable(Zenith* zenith, Widget* widget);
        i32 SetFocusable(Zenith* zenith, Widget* widget);

        i32 IsEnabled(Zenith* zenith, Widget* widget);
        i32 IsVisible(Zenith* zenith, Widget* widget);
        i32 IsInteractable(Zenith* zenith, Widget* widget);
        i32 IsFocusable(Zenith* zenith, Widget* widget);

        i32 GetParent(Zenith* zenith, Widget* widget);
        i32 GetChildren(Zenith* zenith, Widget* widget);
        i32 GetChildrenRecursive(Zenith* zenith, Widget* widget);

        i32 GetAnchor(Zenith* zenith, Widget* widget);
        i32 GetRelativePoint(Zenith* zenith, Widget* widget);

        i32 SetAnchor(Zenith* zenith, Widget* widget);
        i32 SetRelativePoint(Zenith* zenith, Widget* widget);

        i32 IsClipChildren(Zenith* zenith, Widget* widget);
        i32 SetClipChildren(Zenith* zenith, Widget* widget);
        i32 GetClipRect(Zenith* zenith, Widget* widget);
        i32 SetClipRect(Zenith* zenith, Widget* widget);
        i32 GetClipMaskTexture(Zenith* zenith, Widget* widget);
        i32 SetClipMaskTexture(Zenith* zenith, Widget* widget);

        i32 GetPos(Zenith* zenith, Widget* widget);
        i32 GetPosX(Zenith* zenith, Widget* widget);
        i32 GetPosY(Zenith* zenith, Widget* widget);

        i32 SetPos(Zenith* zenith, Widget* widget);
        i32 SetPosX(Zenith* zenith, Widget* widget);
        i32 SetPosY(Zenith* zenith, Widget* widget);

        // TODO: Rename these to GetPos/SetPos, and the previous ones to GetLocalPos/SetLocalPos
        i32 GetWorldPos(Zenith* zenith, Widget* widget);
        i32 GetWorldPosX(Zenith* zenith, Widget* widget);
        i32 GetWorldPosY(Zenith* zenith, Widget* widget);

        i32 SetWorldPos(Zenith* zenith, Widget* widget);
        i32 SetWorldPosX(Zenith* zenith, Widget* widget);
        i32 SetWorldPosY(Zenith* zenith, Widget* widget);

        i32 SetPos3D(Zenith* zenith, Widget* widget);

        i32 ForceRefresh(Zenith* zenith, Widget* widget);
    }

    static LuaRegister<Widget> widgetMethods[] =
    {
        { "SetEnabled", WidgetMethods::SetEnabled },
        { "SetVisible", WidgetMethods::SetVisible },
        { "SetInteractable", WidgetMethods::SetInteractable },
        { "SetFocusable", WidgetMethods::SetFocusable },

        { "IsEnabled", WidgetMethods::IsEnabled },
        { "IsVisible", WidgetMethods::IsVisible },
        { "IsInteractable", WidgetMethods::IsInteractable },
        { "IsFocusable", WidgetMethods::IsFocusable },

        { "GetParent", WidgetMethods::GetParent },
        { "GetChildren", WidgetMethods::GetChildren },
        { "GetChildrenRecursive", WidgetMethods::GetChildrenRecursive },

        { "GetAnchor", WidgetMethods::GetAnchor },
        { "GetRelativePoint", WidgetMethods::GetRelativePoint },

        { "SetAnchor", WidgetMethods::SetAnchor },
        { "SetRelativePoint", WidgetMethods::SetRelativePoint },

        { "IsClipChildren", WidgetMethods::IsClipChildren },
        { "SetClipChildren", WidgetMethods::SetClipChildren },
        { "GetClipRect", WidgetMethods::GetClipRect },
        { "SetClipRect", WidgetMethods::SetClipRect },
        { "GetClipMaskTexture", WidgetMethods::GetClipMaskTexture },
        { "SetClipMaskTexture", WidgetMethods::SetClipMaskTexture },

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

        { "SetPos3D", WidgetMethods::SetPos3D },

        { "ForceRefresh", WidgetMethods::ForceRefresh }
    };

    namespace WidgetInputMethods
    {
        i32 SetOnMouseDown(Zenith* zenith, Widget* widget);
        i32 SetOnMouseUp(Zenith* zenith, Widget* widget);
        i32 SetOnMouseHeld(Zenith* zenith, Widget* widget);
        i32 SetOnMouseScroll(Zenith* zenith, Widget* widget);

        i32 SetOnHoverBegin(Zenith* zenith, Widget* widget);
        i32 SetOnHoverEnd(Zenith* zenith, Widget* widget);
        i32 SetOnHoverHeld(Zenith* zenith, Widget* widget);

        i32 SetOnFocusBegin(Zenith* zenith, Widget* widget);
        i32 SetOnFocusEnd(Zenith* zenith, Widget* widget);
        i32 SetOnFocusHeld(Zenith* zenith, Widget* widget);

        i32 SetOnKeyboard(Zenith* zenith, Widget* widget);
    }

    static LuaRegister<Widget> widgetInputMethods[] =
    {
        { "SetOnMouseDown", WidgetInputMethods::SetOnMouseDown },
        { "SetOnMouseUp", WidgetInputMethods::SetOnMouseUp },
        { "SetOnMouseHeld", WidgetInputMethods::SetOnMouseHeld },
        { "SetOnMouseScroll", WidgetInputMethods::SetOnMouseScroll },

        { "SetOnHoverBegin", WidgetInputMethods::SetOnHoverBegin },
        { "SetOnHoverEnd", WidgetInputMethods::SetOnHoverEnd },
        { "SetOnHoverHeld", WidgetInputMethods::SetOnHoverHeld },

        { "SetOnFocusBegin", WidgetInputMethods::SetOnFocusBegin },
        { "SetOnFocusEnd", WidgetInputMethods::SetOnFocusEnd },
        { "SetOnFocusHeld", WidgetInputMethods::SetOnFocusHeld },

        { "SetOnKeyboard", WidgetInputMethods::SetOnKeyboard }
    };
}