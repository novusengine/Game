#include "Shadows/SVSM.inc.slang"

// Marks pages under moved or animated instances as INVALID. One thread per (AABB, clipmap) pair,
// each ORs the flag over the covered page rect. Page granularity is coarse (quarter meters at the
// finest clipmap) so plain world-magnitude light-space math is precise enough here.

[[vk::push_constant]] SVSMConstants _constants;

[[vk::binding(0, PER_PASS)]] RWStructuredBuffer<SVSMData> _svsmData;
[[vk::binding(1, PER_PASS)]] RWStructuredBuffer<uint> _pageTable;
[[vk::binding(2, PER_PASS)]] StructuredBuffer<float4> _dirtyAABBs; // Pairs of world (min, max)

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

    float3 aabbMin = _dirtyAABBs[aabbIndex * 2 + 0].xyz;
    float3 aabbMax = _dirtyAABBs[aabbIndex * 2 + 1].xyz;

    uint pageTableSize = _constants.pageTableSize;
    float pageWorld = _svsmData[0].pageWorld[clipmapIndex];
    int2 anchorPageMin = int2(_svsmData[0].anchorPageMinX[clipmapIndex], _svsmData[0].anchorPageMinY[clipmapIndex]);

    int2 pageMin;
    int2 pageMax;
    if (!SVSMComputeAABBPageRect(aabbMin, aabbMax, _constants.lightDirection.xyz, pageWorld, anchorPageMin, pageTableSize, pageMin, pageMax))
    {
        return; // Outside this clipmap's window
    }

    // A huge mover would loop for thousands of pages from one thread, fall back to a full
    // invalidation instead. UpdateA consumes frameInvalidationFlags after this pass's barrier
    int2 span = pageMax - pageMin + int2(1, 1);
    if (span.x * span.y > 1024)
    {
        InterlockedOr(_svsmData[0].frameInvalidationFlags, SVSM_CAUSE_AABB_OVERFLOW);
        InterlockedOr(_svsmData[0].statsInvalidationCause, SVSM_CAUSE_AABB_OVERFLOW);
        return;
    }

    for (int y = pageMin.y; y <= pageMax.y; y++)
    {
        for (int x = pageMin.x; x <= pageMax.x; x++)
        {
            int2 slot = WrapPageSlot(anchorPageMin + int2(x, y), pageTableSize);
            InterlockedOr(_pageTable[PageEntryIndex(clipmapIndex, slot, pageTableSize)], SVSM_PAGE_INVALID);
        }
    }
}
