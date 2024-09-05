#pragma once
#include "Game/UI/Box.h"

#include <Base/Math/Color.h>
#include <Base/Types.h>

#include <optional>

namespace ECS::Components::UI
{
    struct PanelTemplate
    {
    public:
        struct SetFlags
        {
            u8 background : 1 = 0;
            u8 foreground : 1 = 0;
            u8 color : 1 = 0;
            u8 cornerRadius : 1 = 0;
            u8 texCoords : 1 = 0;
        };
        SetFlags setFlags;

        std::string background;
        std::string foreground;
        Color color;
        f32 cornerRadius;
        ::UI::Box texCoords;

        std::string onClickTemplate;
        std::string onHoverTemplate;
        std::string onUninteractableTemplate;

        i32 onMouseDownEvent = -1;
        i32 onMouseUpEvent = -1;
        i32 onMouseHeldEvent = -1;

        i32 onHoverBeginEvent = -1;
        i32 onHoverEndEvent = -1;
        i32 onHoverHeldEvent = -1;
    };
}