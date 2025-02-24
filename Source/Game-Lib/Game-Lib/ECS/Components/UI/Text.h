#pragma once
#include <Base/Types.h>

namespace ECS::Components::UI
{
    struct Text
    {
    public:
        std::string text;
        u32 layer;

        u32 templateIndex;

        i32 gpuVertexIndex = -1;
        i32 gpuDataIndex = -1;

        i32 numCharsNonWhitespace = -1;
        i32 numCharsNewLine = -1;

        bool hasGrown = false;
        bool sizeChanged = true;
    };
}