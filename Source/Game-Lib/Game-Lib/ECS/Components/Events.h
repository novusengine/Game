#pragma once
#include <Base/Types.h>

namespace ECS::Components
{
    struct DiscoveredModelsCompleteEvent { };

    struct MapLoadedEvent
    {
    public:
        u32 mapId;
    };
}