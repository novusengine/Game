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

        // This widget's rect unioned with every descendant's subtree bound (a clip source clamps to
        // its clip rect). Maintained by UpdateBoundingRects; used by the hover walk to skip subtrees
        // the cursor isn't over.
        vec2 subtreeMin = vec2(0.0f);
        vec2 subtreeMax = vec2(0.0f);
    };
}