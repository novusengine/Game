#pragma once

#include <Base/Types.h>

#include <vector>

namespace RenderScenes
{
    struct StableRange
    {
        u32 offset = 0;
        u32 count = 0;

        explicit operator bool() const { return count != 0; }
    };

    // Owns CPU-side free-range metadata for stable, non-compacting Scene array allocations.
    // It recycles storage while keeping the GPU-visible offsets of live allocations unchanged.
    class StableRangeAllocator
    {
      public:
        StableRange Allocate(u32 count);
        void Free(StableRange range);

        u32 GetAddressSpaceSize() const { return _addressSpaceSize; }
        u32 GetFreeCount() const;

      private:
        void Coalesce();
        void TrimTail();

        std::vector<StableRange> _freeRanges;
        u32 _addressSpaceSize = 0;
    };
} // namespace RenderScenes
