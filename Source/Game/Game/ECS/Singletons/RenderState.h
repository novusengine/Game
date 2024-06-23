#pragma once
#include <Base/Types.h>

namespace ECS::Singletons
{
    struct RenderState
    {
    public:
        u64 frameNumber = 0;
    };
}