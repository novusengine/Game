#include "Shadows/SVSM.inc.slang"

// Clears this frame's dirty pages in the physical pool, dispatched indirectly from the clear
// list UpdateB filled: one workgroup per dirty page. Cleared depth 0 (reversed Z far) reads as
// fully lit, so a dirty page with no casters is still correct.

[[vk::push_constant]] SVSMConstants _constants;

[[vk::binding(0, PER_PASS)]] StructuredBuffer<uint> _clearList; // [0..2] = indirect dispatch args, physical pages from [4]
[[vk::binding(1, PER_PASS)]] RWTexture2D<uint> _pagePool;

struct CSInput
{
    uint3 groupID : SV_GroupID;
    uint groupIndex : SV_GroupIndex;
};

[shader("compute")]
[numthreads(256, 1, 1)]
void main(CSInput input)
{
    uint physicalPage = _clearList[4 + input.groupID.x];

    uint pageSize = _constants.pageSize;
    uint poolPagesPerRow = _constants.poolPagesPerRow;
    uint2 physicalBase = uint2(physicalPage % poolPagesPerRow, physicalPage / poolPagesPerRow) * pageSize;

    uint texelsPerPage = pageSize * pageSize;
    for (uint i = input.groupIndex; i < texelsPerPage; i += 256)
    {
        uint2 texel = uint2(i % pageSize, i / pageSize);
        _pagePool[physicalBase + texel] = 0;
    }
}
