#pragma once
#include <Base/Types.h>

namespace ECS::Components::UI
{
    struct Panel
    {
    public:
        u32 layer;

        u32 templateIndex;

        i32 gpuVertexIndex = -1;
        i32 gpuDataIndex = -1;
    };
}