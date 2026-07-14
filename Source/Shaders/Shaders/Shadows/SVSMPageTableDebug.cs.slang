#include "Shadows/SVSM.inc.slang"

// Draws one clipmap's page table as a colored grid straight into the scene color corner.
// Black = free slot, green = resident (darkens with age), orange = resident but invalidated,
// red = dirty (rendered this frame), yellow border tint = marked this frame, magenta = marked but
// not resident (pool overflow).

struct DebugConstants
{
    uint clipmapIndex;
    uint pageTableSize;
    uint cellSize;
    uint padding0;
    int2 screenOffset;
};

[[vk::push_constant]] DebugConstants _constants;

[[vk::binding(0, PER_PASS)]] StructuredBuffer<uint> _pageTable;
[[vk::binding(1, PER_PASS)]] RWTexture2D<float4> _target;

struct CSInput
{
    uint3 dispatchThreadID : SV_DispatchThreadID;
};

[shader("compute")]
[numthreads(16, 16, 1)]
void main(CSInput input)
{
    uint pageTableSize = _constants.pageTableSize;
    uint cellSize = _constants.cellSize;
    uint regionSize = pageTableSize * cellSize;
    if (any(input.dispatchThreadID.xy >= uint2(regionSize, regionSize)))
    {
        return;
    }

    uint2 targetDim;
    _target.GetDimensions(targetDim.x, targetDim.y);
    int2 targetPixel = _constants.screenOffset + int2(input.dispatchThreadID.xy);
    if (any(targetPixel < int2(0, 0)) || any(targetPixel >= int2(targetDim)))
    {
        return;
    }

    uint2 slot = input.dispatchThreadID.xy / cellSize;
    uint2 cellPixel = input.dispatchThreadID.xy - slot * cellSize;

    // The table is indexed by wrapped slots, displaying slot space directly shows the torus
    uint entry = _pageTable[PageEntryIndex(_constants.clipmapIndex, int2(slot), pageTableSize)];

    float3 color = float3(0.05f, 0.05f, 0.05f);
    if (entry != 0)
    {
        bool resident = (entry & SVSM_PAGE_RESIDENT) != 0;
        bool invalid = (entry & SVSM_PAGE_INVALID) != 0;
        bool marked = (entry & SVSM_PAGE_MARKED) != 0;
        bool dirty = (entry & SVSM_PAGE_DIRTY) != 0;

        if (dirty)
        {
            color = float3(0.9f, 0.1f, 0.1f);
        }
        else if (resident && invalid)
        {
            color = float3(0.9f, 0.5f, 0.1f);
        }
        else if (resident)
        {
            float freshness = 1.0f - float(GetPageAge(entry)) / 255.0f;
            color = float3(0.1f, 0.25f + 0.5f * freshness, 0.1f);
        }
        else if (marked)
        {
            color = float3(0.9f, 0.1f, 0.9f); // Marked but never became resident, pool overflow
        }

        // Marked pages get a bright border ring so the working set pops against cached pages
        bool onCellBorder = any(cellPixel == uint2(0, 0)) || any(cellPixel == uint2(cellSize - 1, cellSize - 1));
        if (marked && onCellBorder)
        {
            color = float3(0.9f, 0.9f, 0.2f);
        }
    }

    _target[uint2(targetPixel)] = float4(color, 1.0f);
}
