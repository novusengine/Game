#pragma once
#include <Base/Math/Geometry.h>
#include <Base/Math/Color.h>

namespace ECS::Components
{
    struct DebugRenderTransform
    {
    public:
        Color color = Color::Blue;
    };
}