#include "common.inc.hlsl"
#include "Include/Culling.inc.hlsl"
#include "Include/PyramidCulling.inc.hlsl"
#include "globalData.inc.hlsl"
#include "Terrain/Shared.inc.hlsl"

struct Constants
{
    uint numTotalInstances;
};

[[vk::push_constant]] Constants _constants;

[[vk::binding(0, PER_PASS)]] StructuredBuffer<InstanceData> _instances;
[[vk::binding(1, PER_PASS)]] StructuredBuffer<uint> _culledInstancesBitMask;

[[vk::binding(2, PER_PASS)]] RWStructuredBuffer<InstanceData> _culledInstances;
[[vk::binding(3, PER_PASS)]] RWByteAddressBuffer _drawCount;

struct CSInput
{
    uint3 dispatchThreadId : SV_DispatchThreadID;
    uint3 groupID : SV_GroupID;
    uint3 groupThreadID : SV_GroupThreadID;
};

[numthreads(32, 1, 1)]
void main(CSInput input)
{
    uint index = input.dispatchThreadId.x;

    if (index >= _constants.numTotalInstances)
        return;

    uint bitMask = _culledInstancesBitMask[input.groupID.x];
    uint bitIndex = input.groupThreadID.x;

    if (bitMask & (1u << bitIndex))
    {
        uint outIndex;
        _drawCount.InterlockedAdd(4, 1, outIndex);

        _culledInstances[outIndex] = _instances[index];
    }
}