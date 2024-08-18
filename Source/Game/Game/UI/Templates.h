#pragma once
#include <Base/Types.h>
#include <Base/Math/Color.h>

namespace UI
{
    struct ButtonTemplate
    {
        std::string panelTemplate;
        std::string textTemplate;
    };

    struct PanelTemplate
    {
        std::string background;
        Color color;
        f32 cornerRadius;
    };

    struct TextTemplate
    {
        std::string font;
        f32 size;
        Color color;
        f32 borderSize;
        f32 borderFade;
        Color borderColor;
    };
}