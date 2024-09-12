#pragma once
#include <Base/Types.h>
#include <Base/Math/Color.h>

namespace UI
{
    struct Box
    {
    public:
        vec2 min = vec2(0, 0);
        vec2 max = vec2(1, 1);
    };
}