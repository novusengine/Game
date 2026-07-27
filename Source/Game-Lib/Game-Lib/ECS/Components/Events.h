#pragma once
#include <Base/Types.h>

namespace ECS::Components
{
    struct DiscoveredModelsCompleteEvent {};
    struct DatabaseReloadEvent {};

    struct MapLoadedEvent
    {
    public:
        u32 mapId;
    };

    enum class MapLoadFailureReason : u8
    {
        MissingDatabaseRecord,
        MissingHeader,
        InvalidHeader,
        MissingBaseModel,
        NoChunks,
        NoAvailableChunks,
    };

    struct MapLoadFailedEvent
    {
    public:
        u32 mapId;
        MapLoadFailureReason reason;
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
