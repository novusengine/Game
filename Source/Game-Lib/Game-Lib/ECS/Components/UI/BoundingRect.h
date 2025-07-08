#pragma once
#include <Base/Types.h>

namespace ECS::Components::UI
{
    struct BoundingRect
    {
    public:
        vec2 min;
        vec2 max;

        // If this widget is hovered and a child of a RenderTarget canvas, this will be min and max offset by the panel that canvas is displayed on.
        // If not, this will be the same as min and max.
        vec2 hoveredMin;
        vec2 hoveredMax;
    };
}