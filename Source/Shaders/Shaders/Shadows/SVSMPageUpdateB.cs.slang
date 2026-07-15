#include "Shadows/SVSM.inc.slang"

// Allocation, one thread per entry, runs after marking: marked non-resident entries pop a
// physical page from the free list and become RESIDENT|DIRTY, marked invalid entries re-dirty in
// place. Dirty pages accumulate the per-clipmap window-local dirty rect that stage 2 uses to
// tighten the render frusta.
//
// Dispatched once per clipmap (allocClipmap), finest first with a barrier between, so under pool
// pressure the starvation always lands on the coarsest ring where the fallback costs the least.
//
// Lifecycle invariant: DIRTY pages are always rendered the same frame they are set, UpdateA
// clears the bits next frame counting on that. Pool exhaustion leaves the entry non-resident and
// counts an overflow, the sampler falls back to a coarser clipmap.

[[vk::push_constant]] SVSMConstants _constants;

[[vk::binding(0, PER_PASS)]] RWStructuredBuffer<SVSMData> _svsmData;
[[vk::binding(1, PER_PASS)]] RWStructuredBuffer<uint> _pageTable;
[[vk::binding(2, PER_PASS)]] RWStructuredBuffer<uint> _freeList;  // [0] = count, entries from [4]
[[vk::binding(3, PER_PASS)]] RWStructuredBuffer<uint> _clearList; // [0..2] = indirect dispatch args, physical pages from [4]

groupshared uint gMarkedCount;
groupshared uint gDirtyCount;
groupshared uint gDeferredCount;
groupshared int gRectMinX;
groupshared int gRectMinY;
groupshared int gRectMaxX;
groupshared int gRectMaxY;

// Clip rects: dirty pages classified as newly-exposed X columns (0), Y rows (1) or other (2),
// each of the three static draws clips its fragments to one of these
groupshared int gClipRectMinX[3];
groupshared int gClipRectMinY[3];
groupshared int gClipRectMaxX[3];
groupshared int gClipRectMaxY[3];

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
        gMarkedCount = 0;
        gDirtyCount = 0;
        gDeferredCount = 0;
        gRectMinX = 0x7FFFFFFF;
        gRectMinY = 0x7FFFFFFF;
        gRectMaxX = -1;
        gRectMaxY = -1;
    }
    if (input.groupIndex < 3)
    {
        gClipRectMinX[input.groupIndex] = 0x7FFFFFFF;
        gClipRectMinY[input.groupIndex] = 0x7FFFFFFF;
        gClipRectMaxX[input.groupIndex] = -1;
        gClipRectMaxY[input.groupIndex] = -1;
    }
    GroupMemoryBarrierWithGroupSync();

    uint pageTableSize = _constants.pageTableSize;
    uint entriesPerClipmap = pageTableSize * pageTableSize;
    uint clipmapIndex = _constants.allocClipmap;
    uint entryIndex = clipmapIndex * entriesPerClipmap + input.dispatchThreadID.x;

    uint entry = input.dispatchThreadID.x < entriesPerClipmap ? _pageTable[entryIndex] : 0;
    if ((entry & SVSM_PAGE_MARKED) != 0)
    {
        InterlockedAdd(gMarkedCount, 1);

        // Render budget: pages past the per-frame cap defer (entry untouched: MARKED keeps it
        // alive, INVALID persists, no alloc) and retry next frame. The coarsest two rings are
        // exempt, they are the whole-scene fallback and only hold a handful of pages. The
        // finest-first dispatch order means the near-camera pages win the budget.
        // No early return, the group barrier below must stay uniform
        bool wantsRender = (entry & SVSM_PAGE_RESIDENT) == 0 || (entry & SVSM_PAGE_INVALID) != 0;
        bool deferred = false;
        if (wantsRender && _constants.renderBudget != 0 && clipmapIndex + 2 < _constants.numClipmaps)
        {
            uint budgetIndex;
            InterlockedAdd(_svsmData[0].statsBudgetUsed, 1, budgetIndex);
            if (budgetIndex >= _constants.renderBudget)
            {
                InterlockedAdd(gDeferredCount, 1);
                deferred = true;
            }
        }

        if (!deferred)
        {
            if ((entry & SVSM_PAGE_RESIDENT) == 0)
            {
                uint previousCount;
                InterlockedAdd(_freeList[0], uint(-1), previousCount);
                if (int(previousCount) <= 0)
                {
                    // Pool exhausted, restore and leave the entry non-resident
                    InterlockedAdd(_freeList[0], 1);
                    InterlockedAdd(_svsmData[0].statsOverflow, 1);
                }
                else
                {
                    uint physicalPage = _freeList[4 + previousCount - 1];
                    entry = PackPageEntry(physicalPage, 0, SVSM_PAGE_RESIDENT | SVSM_PAGE_MARKED | SVSM_PAGE_DIRTY);
                }
            }
            else if ((entry & SVSM_PAGE_INVALID) != 0)
            {
                entry = (entry & ~SVSM_PAGE_INVALID) | SVSM_PAGE_DIRTY;
            }

            if ((entry & SVSM_PAGE_DIRTY) != 0)
            {
                InterlockedAdd(gDirtyCount, 1);

                // Queue the physical page for the indirect clear, one workgroup per page
                uint clearIndex;
                InterlockedAdd(_clearList[0], 1, clearIndex);
                _clearList[4 + clearIndex] = GetPagePhysical(entry);

                // Window-local page coords for the dirty rect
                uint slotLinear = entryIndex - clipmapIndex * entriesPerClipmap;
                int2 slot = int2(slotLinear & (pageTableSize - 1), slotLinear / pageTableSize);
                int2 anchor = int2(_svsmData[0].anchorPageMinX[clipmapIndex], _svsmData[0].anchorPageMinY[clipmapIndex]);
                int2 localPage = WrapPageSlot(slot - anchor, pageTableSize);

                InterlockedMin(gRectMinX, localPage.x);
                InterlockedMin(gRectMinY, localPage.y);
                InterlockedMax(gRectMaxX, localPage.x);
                InterlockedMax(gRectMaxY, localPage.y);

                // Classify for the clip rects: a toroidal slide dirties an L whose bounding box
                // covers most of the window, but its X and Y stripes are thin on their own
                int2 absPage = anchor + localPage;
                int2 prevAnchor = int2(_svsmData[0].prevAnchorPageMinX[clipmapIndex], _svsmData[0].prevAnchorPageMinY[clipmapIndex]);
                bool exposedX = absPage.x < prevAnchor.x || absPage.x >= prevAnchor.x + int(pageTableSize);
                bool exposedY = absPage.y < prevAnchor.y || absPage.y >= prevAnchor.y + int(pageTableSize);
                uint clipRect = exposedX ? 0 : (exposedY ? 1 : 2);

                InterlockedMin(gClipRectMinX[clipRect], localPage.x);
                InterlockedMin(gClipRectMinY[clipRect], localPage.y);
                InterlockedMax(gClipRectMaxX[clipRect], localPage.x);
                InterlockedMax(gClipRectMaxY[clipRect], localPage.y);
            }

            _pageTable[entryIndex] = entry;
        }
    }

    GroupMemoryBarrierWithGroupSync();

    if (input.groupIndex == 0 && clipmapIndex < 8)
    {
        if (gMarkedCount != 0)
        {
            InterlockedAdd(_svsmData[0].statsMarked[clipmapIndex], gMarkedCount);
        }
        if (gDirtyCount != 0)
        {
            InterlockedAdd(_svsmData[0].statsDirty[clipmapIndex], gDirtyCount);
            InterlockedMin(_svsmData[0].dirtyRectMinX[clipmapIndex], gRectMinX);
            InterlockedMin(_svsmData[0].dirtyRectMinY[clipmapIndex], gRectMinY);
            InterlockedMax(_svsmData[0].dirtyRectMaxX[clipmapIndex], gRectMaxX);
            InterlockedMax(_svsmData[0].dirtyRectMaxY[clipmapIndex], gRectMaxY);

            for (uint r = 0; r < 3; r++)
            {
                if (gClipRectMaxX[r] >= 0)
                {
                    InterlockedMin(_svsmData[0].clipRectMinX[r * 8 + clipmapIndex], gClipRectMinX[r]);
                    InterlockedMin(_svsmData[0].clipRectMinY[r * 8 + clipmapIndex], gClipRectMinY[r]);
                    InterlockedMax(_svsmData[0].clipRectMaxX[r * 8 + clipmapIndex], gClipRectMaxX[r]);
                    InterlockedMax(_svsmData[0].clipRectMaxY[r * 8 + clipmapIndex], gClipRectMaxY[r]);
                }
            }
        }
        if (gDeferredCount != 0)
        {
            InterlockedAdd(_svsmData[0].statsDeferred[clipmapIndex], gDeferredCount);
        }
    }
}
