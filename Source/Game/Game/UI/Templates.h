#pragma once
#include "Game/UI/Box.h"

#include <Base/Types.h>
#include <Base/Math/Color.h>

namespace UI
{
    struct ButtonTemplate
    {
    public:
        std::string panelTemplate;
        std::string textTemplate;
    };

    struct PanelTemplate
    {
    public:
        std::string background;
        Color color;
        f32 cornerRadius;
        Box texCoords;
    };

    struct TextTemplate
    {
    public:
        std::string font;
        f32 size;
        Color color;
        f32 borderSize;
        f32 borderFade;
        Color borderColor;
    };
}