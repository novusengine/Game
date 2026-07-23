#include "Shadows/SVSM.inc.slang"

// Page table maintenance, one thread per entry, runs before this frame's marking:
// applies toroidal and global invalidation, ages entries, evicts stale pages back to the free
// list and clears last frame's MARKED/DIRTY bits (safe because dirty pages always rendered the
// frame they were marked). Must run before marking: this is the only pass that read-modify-writes
// entries non-atomically.

[[vk::push_constant]] SVSMConstants _constants;

[[vk::binding(0, PER_PASS)]] RWStructuredBuffer<SVSMData> _svsmData;
[[vk::binding(1, PER_PASS)]] RWStructuredBuffer<uint> _pageTable;
[[vk::binding(2, PER_PASS)]] RWStructuredBuffer<uint> _freeList; // [0] = count, entries from [4]

groupshared uint gResidentCount;
groupshared uint gInvalidatedCount;
groupshared uint gEvictedCount;

struct CSInput
{
    uint3 dispatchThreadID : SV_DispatchThreadID;
    uint groupIndex : SV_GroupIndex;
};

[shader("compute")]
[numthreads(256, 1, 1)]
void main(CSInput input)
{
    if (input.groupIndex == 0)
    {
        gResidentCount = 0;
        gInvalidatedCount = 0;
        gEvictedCount = 0;
    }
    GroupMemoryBarrierWithGroupSync();

    uint pageTableSize = _constants.pageTableSize;
    uint entriesPerClipmap = pageTableSize * pageTableSize;
    uint entryIndex = input.dispatchThreadID.x;
    uint clipmapIndex = entryIndex / entriesPerClipmap; // Group-uniform, entriesPerClipmap is a multiple of 256

    uint entry = _pageTable[entryIndex];
    if (entry != 0)
    {
        bool resident = (entry & SVSM_PAGE_RESIDENT) != 0;
        bool invalid = (entry & SVSM_PAGE_INVALID) != 0;
        bool markedLastFrame = (entry & SVSM_PAGE_MARKED) != 0;
        uint age = markedLastFrame ? 0 : min(GetPageAge(entry) + 1, 255u);

        bool clipmapActive = clipmapIndex < _constants.numClipmaps;
        if (clipmapActive)
        {
            // Toroidal reuse: valid only if the slot maps to the same absolute page under the old
            // and the new window. Handles any slide distance, teleports included
            uint slotLinear = entryIndex - clipmapIndex * entriesPerClipmap;
            int2 slot = int2(slotLinear & (pageTableSize - 1), slotLinear / pageTableSize);

            int2 anchor = int2(_svsmData[0].anchorPageMinX[clipmapIndex], _svsmData[0].anchorPageMinY[clipmapIndex]);
            int2 prevAnchor = int2(_svsmData[0].prevAnchorPageMinX[clipmapIndex], _svsmData[0].prevAnchorPageMinY[clipmapIndex]);
            int2 absNew = anchor + WrapPageSlot(slot - anchor, pageTableSize);
            int2 absOld = prevAnchor + WrapPageSlot(slot - prevAnchor, pageTableSize);

            bool stale = any(absNew != absOld);
            stale = stale || (_svsmData[0].frameInvalidationFlags != 0);
            stale = stale || (_svsmData[0].clipmapInvalidate[clipmapIndex] != 0);

            if (stale && resident && !invalid)
            {
                InterlockedAdd(gInvalidatedCount, 1);
            }
            invalid = invalid || stale;
        }

        // Pressure eviction: when the pool runs dry the stale cache recycles within frames instead
        // of squatting for the full evict age, so the finest clipmaps can always allocate.
        // The age field saturates at 255, an evict age at or above that would never trigger
        uint effectiveEvictAge = min(_constants.evictAge, 254u);
        uint poolPages = _constants.poolPagesPerRow * _constants.poolPagesPerRow;
        uint freeCount = _svsmData[0].statsFreeListCount; // Start-of-frame snapshot from Prepare
        if (freeCount < poolPages / 8)
        {
            effectiveEvictAge = min(effectiveEvictAge, 30u);
        }
        if (freeCount < poolPages / 32)
        {
            effectiveEvictAge = min(effectiveEvictAge, 2u);
        }

        // Invalidated pages that nothing marked are dead weight: their content may live in an old
        // depth mapping (Z-range/sun change) which the sampler refuses, so free them immediately
        // instead of letting them squat for the full evict age. Marked ones re-render in UpdateB
        if (resident && (age > effectiveEvictAge || (invalid && !markedLastFrame)))
        {
            // Return the physical page and free the slot
            uint writeIndex;
            InterlockedAdd(_freeList[0], 1, writeIndex);
            _freeList[4 + writeIndex] = GetPagePhysical(entry);
            InterlockedAdd(gEvictedCount, 1);
            entry = 0;
        }
        else
        {
            uint flags = (resident ? SVSM_PAGE_RESIDENT : 0u) | ((invalid && resident) ? SVSM_PAGE_INVALID : 0u);
            entry = PackPageEntry(GetPagePhysical(entry), age, flags);
            if (resident)
            {
                InterlockedAdd(gResidentCount, 1);
            }
        }

        _pageTable[entryIndex] = entry;
    }

    GroupMemoryBarrierWithGroupSync();

    if (input.groupIndex == 0 && clipmapIndex < 8)
    {
        if (gResidentCount != 0)
        {
            InterlockedAdd(_svsmData[0].statsResident[clipmapIndex], gResidentCount);
        }
        if (gInvalidatedCount != 0)
        {
            InterlockedAdd(_svsmData[0].statsInvalidated[clipmapIndex], gInvalidatedCount);
        }
        if (gEvictedCount != 0)
        {
            InterlockedAdd(_svsmData[0].statsEvicted[clipmapIndex], gEvictedCount);
        }
    }
}
