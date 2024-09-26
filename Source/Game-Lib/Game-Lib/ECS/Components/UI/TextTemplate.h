#pragma once
#include "Game-Lib/UI/Box.h"

#include <Base/Math/Color.h>
#include <Base/Types.h>

namespace ECS::Components::UI
{
    struct TextTemplate
    {
    public:
        struct SetFlags
        {
            u8 font : 1 = 0;
            u8 size : 1 = 0;
            u8 color : 1 = 0;
            u8 borderSize : 1 = 0;
            u8 borderFade : 1 = 0;
            u8 borderColor : 1 = 0;
        };
        SetFlags setFlags;

        std::string font;
        f32 size;
        Color color;
        f32 borderSize;
        f32 borderFade;
        Color borderColor;

        std::string onClickTemplate;
        std::string onHoverTemplate;
        std::string onUninteractableTemplate;

        i32 onMouseDownEvent = -1;
        i32 onMouseUpEvent = -1;
        i32 onMouseHeldEvent = -1;

        i32 onHoverBeginEvent = -1;
        i32 onHoverEndEvent = -1;
        i32 onHoverHeldEvent = -1;

        i32 onFocusBeginEvent = -1;
        i32 onFocusEndEvent = -1;
        i32 onFocusHeldEvent = -1;
    };
}