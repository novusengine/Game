#pragma once

#include <Base/Types.h>

namespace Util::JoltMemoryTelemetry
{
    struct Stats
    {
    public:
        u64 currentBytes = 0;
        u64 peakBytes = 0;
        u64 totalAllocatedBytes = 0;
        u64 totalFreedBytes = 0;
        u64 liveAllocations = 0;
        u64 totalAllocations = 0;
    };

    void RegisterAllocator();
    bool IsEnabled();
    Stats GetStats();
}
