#pragma once
#include <Base/Types.h>

namespace ECS::Components
{
    struct DiscoveredModelsCompleteEvent {};
    struct RefreshDatabaseEvent {};

    struct MapLoadedEvent
    {
    public:
        u32 mapId;
    };

    struct ModelLoadedEventFlags
    {
    public:
        u8 loaded : 1 = 0;
        u8 rollback : 1 = 0;
        u8 staticModel : 1 = 0;
    };
    struct ModelLoadedEvent
    {
    public:
        ModelLoadedEventFlags flags = { 0 };
    };
}