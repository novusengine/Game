
#include "DescriptorSet/Global.inc.slang"

#include "Include/Common.inc.slang"
#include "Include/Culling.inc.slang"
#include "Include/PyramidCulling.inc.slang"
#include "Terrain/TerrainShared.inc.slang"

struct Constants
{
    uint numTotalInstances;
    uint bitmaskOffset;
    uint diffAgainstPrev;
};

[[vk::push_constant]] Constants _constants;

[[vk::binding(0, PER_PASS)]] StructuredBuffer<InstanceData> _instances;
[[vk::binding(1, PER_PASS)]] StructuredBuffer<uint> _culledInstancesBitMask;
[[vk::binding(2, PER_PASS)]] StructuredBuffer<uint> _prevCulledInstancesBitMask;

[[vk::binding(3, PER_PASS)]] RWStructuredBuffer<InstanceData> _culledInstances;
[[vk::binding(4, PER_PASS)]] RWByteAddressBuffer _drawCount;

struct CSInput
{
    uint3 dispatchThreadId : SV_DispatchThreadID;
    uint3 groupID : SV_GroupID;
    uint3 groupThreadID : SV_GroupThreadID;
};

[shader("compute")]
[numthreads(32, 1, 1)]
void main(CSInput input)
{
    uint index = input.dispatchThreadId.x;

    if (index >= _constants.numTotalInstances)
        return;

    uint bitMaskIndex = _constants.bitmaskOffset + input.groupID.x;
    uint bitMask = _culledInstancesBitMask[bitMaskIndex];
    uint prevBitMask = _prevCulledInstancesBitMask[bitMaskIndex];
    uint bitIndex = input.groupThreadID.x;

    bool isVisible = bitMask & (1u << bitIndex);
    bool wasAlreadyDrawn = prevBitMask & (1u << bitIndex);

    bool shouldDraw = isVisible && (!wasAlreadyDrawn || !_constants.diffAgainstPrev);
    if (shouldDraw)
    {
        uint outIndex;
        _drawCount.InterlockedAdd(4, 1, outIndex);

        _culledInstances[outIndex] = _instances[index];
    }
}