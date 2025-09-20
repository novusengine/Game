#pragma once
#include <Base/Types.h>
#include <Base/Util/Reflection.h>

#include <robinhood/robinhood.h>
#include <limits>

namespace ECS::Components
{
    struct ModelFlags
    {
    public:
        u8 loaded : 1 = 0;
        u8 visible : 1 = 1;
        u8 forcedTransparency : 1 = 0;
    };
    struct Model
    {
    public:
        ModelFlags flags = { 0 };
        u32 modelID = std::numeric_limits<u32>().max();
        u32 instanceID = std::numeric_limits<u32>().max();
        u32 modelHash = std::numeric_limits<u32>().max();
        f32 opacity = 1.0f;
        f32 scale = 1.0f;
    };

    struct ModelQueuedGeometryGroups
    {
    public:
        robin_hood::unordered_set<u32> enabledGroupIDs;
    };
}

REFL_TYPE(ECS::Components::Model)
    REFL_FIELD(modelID, Reflection::ReadOnly())
    REFL_FIELD(instanceID, Reflection::ReadOnly())
REFL_END