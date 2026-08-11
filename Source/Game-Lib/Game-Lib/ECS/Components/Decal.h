#pragma once
#include <Base/Types.h>
#include <Base/Math/Color.h>

namespace ECS::Components
{
    struct Decal
    {
        std::string texturePath;
        Color colorMultiplier = Color::White;
        hvec2 thresholdMinMax = hvec2(0.0f, 1.0f);
        hvec2 minUV = hvec2(0.0f, 0.0f);
        hvec2 maxUV = hvec2(1.0f, 1.0f);
        f32 opacity = 1.0f;
        i32 priority = 0;
        u32 flags = 0; // DecalFlags
    };

    struct DirtyDecal {};
}
