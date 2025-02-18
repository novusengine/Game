#pragma once
#include <Base/Types.h>
#include <Base/Util/Reflection.h>

#include <limits>

namespace ECS::Components
{
    struct Model
    {
    public:
        u32 modelID = std::numeric_limits<u32>().max();
        u32 instanceID = std::numeric_limits<u32>().max();
        u32 modelHash = std::numeric_limits<u32>().max();
        bool visible = true;
        bool forcedTransparency = false;
    };
}

REFL_TYPE(ECS::Components::Model)
    REFL_FIELD(modelID, Reflection::ReadOnly())
    REFL_FIELD(instanceID, Reflection::ReadOnly())
REFL_END