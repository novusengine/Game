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

    struct Widget
    {
    public:
        WidgetType type;
        Scripting::UI::Widget* scriptWidget = nullptr;
    };

    struct WidgetRoot {};
    struct DirtyWidgetTransform {};
    struct DirtyWidgetData {};
}