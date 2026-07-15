#define GEOMETRY_PASS 1

#include "DescriptorSet/Global.inc.slang"

#include "Shadows/SVSM.inc.slang"

// SVSM page render: no attachments, the fragment writes reversed-Z depth into the physical page
// pool with image atomics. Paired with the SHADOW_PASS=1 vertex permutation, viewport spans the
// full virtual texture so SV_Position.xy is the window-local virtual texel.

struct Constants
{
    uint viewIndex;     // 0 = main camera, clipmap k renders as view k + 1
    uint svsmRectIndex; // Consumed by the vertex shader's clip rect, declared here so the push block matches
};

[[vk::push_constant]] Constants _constants;

[[vk::binding(0, PER_PASS)]] StructuredBuffer<SVSMData> _svsmData;
[[vk::binding(1, PER_PASS)]] StructuredBuffer<uint> _pageTable;
[[vk::binding(2, PER_PASS)]] RWTexture2D<uint> _pagePool;

struct PSInput
{
    float4 position : SV_Position;
};

[shader("fragment")]
void main(PSInput input)
{
    SVSMWritePageDepth(input.position.xyz, _constants.viewIndex - 1, SVSM_PAGE_RESIDENT | SVSM_PAGE_DIRTY, _svsmData[0].configPoolPagesPerRow, _svsmData, _pageTable, _pagePool);
}
