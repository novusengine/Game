#pragma once
#include <Base/Types.h>

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
    };

    struct WidgetRoot {};
    struct DirtyWidget {};
}