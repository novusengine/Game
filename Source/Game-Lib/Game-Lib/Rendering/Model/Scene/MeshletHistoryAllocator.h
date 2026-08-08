#pragma once

#include <Base/Types.h>

#include <span>
#include <vector>

namespace ModelScene
{
    struct MeshletHistoryRange
    {
        u32 wordOffset = 0;
        u32 wordCount = 0;

        explicit operator bool() const { return wordCount != 0; }
    };

    struct MeshletHistoryAllocatorStats
    {
        u32 liveWords = 0;
        u32 retiredWords = 0;
        u32 freeWords = 0;
        u32 addressSpaceWords = 0;
        u32 highWaterWords = 0;
        u32 pendingClearRanges = 0;
        u32 freeRanges = 0;
    };

    // Owns CPU-side allocation and deferred-reuse metadata for the Scene meshlet-history address space.
    // The history preserves prior visibility so temporal occlusion culling can classify each meshlet.
    class MeshletHistoryAllocator
    {
      public:
        void Reserve(u32 rangeCount);
        MeshletHistoryRange Allocate(u32 wordCount);
        void Retire(MeshletHistoryRange range, u64 retireValue);
        void ReleaseRetired(u64 completedValue);
        void RequestClear(MeshletHistoryRange range);

        std::span<const MeshletHistoryRange> GetPendingClears() const { return _pendingClears; }
        void AcknowledgePendingClears() { _pendingClears.clear(); }
        MeshletHistoryAllocatorStats GetStats() const;

      private:
        struct RetiredRange
        {
            MeshletHistoryRange range;
            u64 retireValue = 0;
        };

        void AddFreeRange(MeshletHistoryRange range);
        void CoalesceFreeRanges();
        void TrimFreeTail();

        std::vector<MeshletHistoryRange> _freeRanges;
        std::vector<RetiredRange> _retiredRanges;
        std::vector<MeshletHistoryRange> _pendingClears;
        u32 _liveWords = 0;
        u32 _retiredWords = 0;
        u32 _addressSpaceWords = 0;
        u32 _highWaterWords = 0;
    };
} // namespace ModelScene
