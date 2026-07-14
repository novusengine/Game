#include "Shadows/SVSM.inc.slang"

// Dynamic page lifecycle, one thread per entry, runs after SVSMDynamicMark. Dispatched TWICE:
// dynamicPhase 0 releases pages nobody marked, dynamicPhase 1 allocates marked entries from the
// dynamic free list and queues every live page for the per-frame clear+render. The phases must
// not share a dispatch: a pop racing a push can read the list slot before the pushed physical
// index lands, aliasing one physical page under two table entries (ghost shadows).
// Dynamic pages are transient by design — they re-render every frame, so there is no age, no
// DIRTY bookkeeping and no invalidation. Clears MARKED on write so next frame starts clean.

[[vk::push_constant]] SVSMConstants _constants;

[[vk::binding(0, PER_PASS)]] RWStructuredBuffer<SVSMData> _svsmData;
[[vk::binding(1, PER_PASS)]] RWStructuredBuffer<uint> _dynamicPageTable;
[[vk::binding(2, PER_PASS)]] RWStructuredBuffer<uint> _dynamicFreeList; // [0] = count, entries from [4]
[[vk::binding(3, PER_PASS)]] RWStructuredBuffer<uint> _dynamicClearList; // [0..2] = indirect dispatch args, physical pages from [4]

groupshared uint gLiveCount;
groupshared int gRectMinX;
groupshared int gRectMinY;
groupshared int gRectMaxX;
groupshared int gRectMaxY;

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
        gLiveCount = 0;
        gRectMinX = 0x7FFFFFFF;
        gRectMinY = 0x7FFFFFFF;
        gRectMaxX = -1;
        gRectMaxY = -1;
    }
    GroupMemoryBarrierWithGroupSync();

    uint pageTableSize = _constants.pageTableSize;
    uint entriesPerClipmap = pageTableSize * pageTableSize;
    uint entryIndex = input.dispatchThreadID.x;
    uint clipmapIndex = entryIndex / entriesPerClipmap; // Group-uniform, entriesPerClipmap is a multiple of 256

    uint entry = _dynamicPageTable[entryIndex];
    // No early returns before the group barrier below. Phase 0 only releases; allocation and
    // bookkeeping run in phase 1, after the free-list barrier between the two dispatches
    if (entry != 0 && _constants.dynamicPhase == 0)
    {
        bool marked = (entry & SVSM_PAGE_MARKED) != 0;
        bool resident = (entry & SVSM_PAGE_RESIDENT) != 0;
        if (!marked && resident)
        {
            // No dynamic caster overlaps this page anymore, return it
            uint writeIndex;
            InterlockedAdd(_dynamicFreeList[0], 1, writeIndex);
            _dynamicFreeList[4 + writeIndex] = GetPagePhysical(entry);
            _dynamicPageTable[entryIndex] = 0;
        }
    }
    else if (entry != 0)
    {
        bool marked = (entry & SVSM_PAGE_MARKED) != 0;
        bool resident = (entry & SVSM_PAGE_RESIDENT) != 0;

        if (marked && !resident)
        {
            uint previousCount;
            InterlockedAdd(_dynamicFreeList[0], uint(-1), previousCount);
            if (int(previousCount) <= 0)
            {
                InterlockedAdd(_dynamicFreeList[0], 1);
                InterlockedAdd(_svsmData[0].statsDynamicOverflow, 1);
                entry = 0; // Stays non-resident, the sampler simply sees no dynamic caster here
            }
            else
            {
                uint physicalPage = _dynamicFreeList[4 + previousCount - 1];
                entry = PackPageEntry(physicalPage, 0, SVSM_PAGE_RESIDENT);
                resident = true;
            }
        }
        else if (marked && resident)
        {
            entry = PackPageEntry(GetPagePhysical(entry), 0, SVSM_PAGE_RESIDENT); // Clear MARKED for next frame
        }

        if (resident)
        {
            // Every live dynamic page clears and re-renders this frame
            uint clearIndex;
            InterlockedAdd(_dynamicClearList[0], 1, clearIndex);
            _dynamicClearList[4 + clearIndex] = GetPagePhysical(entry);

            InterlockedAdd(gLiveCount, 1);

            if (clipmapIndex < _constants.numClipmaps)
            {
                uint slotLinear = entryIndex - clipmapIndex * entriesPerClipmap;
                int2 slot = int2(slotLinear & (pageTableSize - 1), slotLinear / pageTableSize);
                int2 anchor = int2(_svsmData[0].anchorPageMinX[clipmapIndex], _svsmData[0].anchorPageMinY[clipmapIndex]);
                int2 localPage = WrapPageSlot(slot - anchor, pageTableSize);

                InterlockedMin(gRectMinX, localPage.x);
                InterlockedMin(gRectMinY, localPage.y);
                InterlockedMax(gRectMaxX, localPage.x);
                InterlockedMax(gRectMaxY, localPage.y);
            }
        }

        _dynamicPageTable[entryIndex] = entry;
    }

    GroupMemoryBarrierWithGroupSync();

    if (input.groupIndex == 0 && clipmapIndex < 8 && gLiveCount != 0)
    {
        InterlockedAdd(_svsmData[0].statsDynamicLive[clipmapIndex], gLiveCount);
        InterlockedAdd(_svsmData[0].statsDynamicTotal, gLiveCount);
        InterlockedMin(_svsmData[0].dynamicRectMinX[clipmapIndex], gRectMinX);
        InterlockedMin(_svsmData[0].dynamicRectMinY[clipmapIndex], gRectMinY);
        InterlockedMax(_svsmData[0].dynamicRectMaxX[clipmapIndex], gRectMaxX);
        InterlockedMax(_svsmData[0].dynamicRectMaxY[clipmapIndex], gRectMaxY);
    }
}
