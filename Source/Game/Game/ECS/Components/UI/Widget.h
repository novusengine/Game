#pragma once
#include <Base/Types.h>

namespace Scripting::UI
{
    struct Widget;
}

namespace ECS::Components::UI
{
    enum class WidgetType : u8
    {
        Canvas,
        Panel,
        Text,

        None
    };

    enum class WidgetFlags : u8
    {
        None = 0,

        Enabled = 1 << 0,
        Visible = 1 << 1,
        Interactable = 1 << 2,
        Resizable = 1 << 3,

        Default = Enabled | Visible | Interactable,
    };
    DECLARE_GENERIC_BITWISE_OPERATORS(WidgetFlags);

    struct Widget
    {
    public:
        WidgetType type;
        WidgetFlags flags = WidgetFlags::Default;
        Scripting::UI::Widget* scriptWidget = nullptr;

        // Non mutable helper functions
        inline bool IsEnabled() const { return (flags & WidgetFlags::Enabled) == WidgetFlags::Enabled; }
        inline bool IsVisible() const { return IsEnabled() && (flags & WidgetFlags::Visible) == WidgetFlags::Visible; }
        inline bool IsInteractable() const { return IsVisible() && (flags & WidgetFlags::Interactable) == WidgetFlags::Interactable; }
        inline bool IsResizable() const { return IsInteractable() && (flags & WidgetFlags::Resizable) == WidgetFlags::Resizable; }
    };

    struct WidgetRoot {};
    struct DirtyWidgetTransform {};
    struct DirtyWidgetData {};
    struct DirtyWidgetFlags {};
}