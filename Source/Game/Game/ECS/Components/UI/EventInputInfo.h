#pragma once
#include <Base/Types.h>

namespace ECS::Components::UI
{
    struct EventInputInfo
    {
    public:
        // Templates
        u32 onClickTemplateHash = 0;
        u32 onHoverTemplateHash = 0;

        // Lua Event Refs
        i32 onMouseDownEvent = -1;
        i32 onMouseUpEvent = -1;
        i32 onMouseHeldEvent = -1;

        i32 onHoverBeginEvent = -1;
        i32 onHoverEndEvent = -1;
        i32 onHoverEvent = -1;
    };
}