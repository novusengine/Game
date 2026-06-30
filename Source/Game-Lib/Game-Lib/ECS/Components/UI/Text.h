#pragma once
#include <Base/Types.h>

namespace ECS::Components::UI
{
    struct Text
    {
    public:
        std::string rawText;
        std::string text;
        u32 layer;

        u32 templateIndex;

        i32 gpuVertexIndex = -1;
        i32 gpuDataIndex = -1;

        i32 gpuVertexCapacity = 0; // allocated vertex slots at gpuVertexIndex
        i32 gpuDataCapacity = 0;   // allocated draw-data slots at gpuDataIndex

        i32 bucketSlot = -1; // index into the owning bucket's finalSortedArgs, for incremental patching

        i32 numCharsNonWhitespace = -1;
        i32 numCharsNewLine = -1;

        bool sizeChanged = true;
    };
}