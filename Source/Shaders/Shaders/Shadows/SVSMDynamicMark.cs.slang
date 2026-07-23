#include "Shadows/SVSM.inc.slang"

// Marks dynamic pages: for each dynamic caster AABB and clipmap, pages that the caster overlaps
// AND that are visible this frame (marked in the static table) get MARKED in the dynamic table.
// Runs after SVSMPageMark so the static visibility marks are current. One thread per
// (AABB, clipmap) pair, same light-space rect math as SVSMInvalidateAABBs — for an ortho light a
// caster's light-space XY equals its shadow's XY, so this covers where the shadow lands.

[[vk::push_constant]] SVSMConstants _constants;

[[vk::binding(0, PER_PASS)]] StructuredBuffer<SVSMData> _svsmData;
[[vk::binding(1, PER_PASS)]] StructuredBuffer<uint> _pageTable;          // Static table, read for visibility
[[vk::binding(2, PER_PASS)]] RWStructuredBuffer<uint> _dynamicPageTable;
[[vk::binding(3, PER_PASS)]] StructuredBuffer<float4> _dynamicAABBs;     // Pairs of world (min, max)

struct CSInput
{
    uint3 dispatchThreadID : SV_DispatchThreadID;
};

[shader("compute")]
[numthreads(64, 1, 1)]
void main(CSInput input)
{
    uint aabbIndex = input.dispatchThreadID.x;
    uint clipmapIndex = input.dispatchThreadID.y;
    if (aabbIndex >= _constants.numDirtyAABBs || clipmapIndex >= _constants.numClipmaps)
    {
        return;
    }

    float3 aabbMin = _dynamicAABBs[aabbIndex * 2 + 0].xyz;
    float3 aabbMax = _dynamicAABBs[aabbIndex * 2 + 1].xyz;

    uint pageTableSize = _constants.pageTableSize;
    float pageWorld = _svsmData[0].pageWorld[clipmapIndex];
    int2 anchorPageMin = int2(_svsmData[0].anchorPageMinX[clipmapIndex], _svsmData[0].anchorPageMinY[clipmapIndex]);

    int2 pageMin;
    int2 pageMax;
    if (!SVSMComputeAABBPageRect(aabbMin, aabbMax, _constants.lightDirection.xyz, pageWorld, anchorPageMin, pageTableSize, pageMin, pageMax))
    {
        return; // Outside this clipmap's window
    }

    // A huge dynamic mover marks at most this many pages per ring, coarser clipmaps cover it
    // (span quarters per ring). The CPU classifier routes casters that would exceed this in EVERY
    // ring to the static path instead, so the cutoff never orphans a caster from both pools
    int2 span = pageMax - pageMin + int2(1, 1);
    if (span.x * span.y > 1024)
    {
        return;
    }

    for (int y = pageMin.y; y <= pageMax.y; y++)
    {
        for (int x = pageMin.x; x <= pageMax.x; x++)
        {
            int2 slot = WrapPageSlot(anchorPageMin + int2(x, y), pageTableSize);
            uint entryIndex = PageEntryIndex(clipmapIndex, slot, pageTableSize);

            // Only where something visible actually samples this frame
            if ((_pageTable[entryIndex] & SVSM_PAGE_MARKED) != 0)
            {
                InterlockedOr(_dynamicPageTable[entryIndex], SVSM_PAGE_MARKED);
            }
        }
    }
}
