#pragma once
#include <Base/Types.h>

namespace ECS::Components::UI
{
    struct Panel
    {
    public:
        u32 layer;

        i32 templateIndex = -1;

        i32 gpuVertexIndex = -1;
        i32 gpuDataIndex = -1;
    };
}