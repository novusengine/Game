#pragma once

#include <Base/Types.h>

namespace ECS::Util::UIInput
{
    inline bool IsWithin(const vec2& point, const vec2& min, const vec2& max)
    {
        return point.x >= min.x && point.x < max.x && point.y >= min.y && point.y < max.y;
    }

    inline bool PhysicalTopLeftToReference(const vec2& physicalPosition, const vec2& renderSize, const vec2& referenceSize, vec2& outReferencePosition)
    {
        if (renderSize.x <= 0.0f || renderSize.y <= 0.0f)
            return false;

        const vec2 physicalBottomLeft(physicalPosition.x, renderSize.y - physicalPosition.y);
        outReferencePosition = physicalBottomLeft / renderSize * referenceSize;
        return true;
    }

    inline bool ReferenceToPhysicalBottomLeft(const vec2& referencePosition, const vec2& renderSize, const vec2& referenceSize, vec2& outPhysicalPosition)
    {
        if (renderSize.x <= 0.0f || renderSize.y <= 0.0f || referenceSize.x <= 0.0f || referenceSize.y <= 0.0f)
            return false;

        outPhysicalPosition = referencePosition / referenceSize * renderSize;
        return true;
    }
}
