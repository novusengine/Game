permutation IS_INDEXED = [0, 1];

#include "DescriptorSet/Global.inc.slang"

#include "Include/Common.inc.slang"
#include "Include/Culling.inc.slang"
#include "Include/PyramidCulling.inc.slang"

struct Constants
{
    uint numTotalInstances;
    uint baseInstanceLookupOffset; // Byte offset into drawCallDatas where the baseInstanceLookup is stored
    uint drawCallDataSize;
};

struct InstanceRef
{
    uint instanceID;
    uint drawID;
    uint extraID;
    uint padding;
};

#if IS_INDEXED
typedef IndexedDraw DrawType;
#else
typedef Draw DrawType;
#endif

[[vk::push_constant]] Constants _constants;

[[vk::binding(0, PER_PASS)]] StructuredBuffer<InstanceRef> _instanceRefTable;
[[vk::binding(1, PER_PASS)]] StructuredBuffer<uint> _culledDrawCallsBitMask;
[[vk::binding(2, PER_PASS)]] ByteAddressBuffer _drawCallDatas;

[[vk::binding(3, PER_PASS)]] RWByteAddressBuffer _culledInstanceCounts; // One uint per draw call
[[vk::binding(4, PER_PASS)]] RWStructuredBuffer<uint> _culledInstanceLookupTable; // One uint per instance, contains instanceRefID of what survives culling, and thus can get reordered

struct CSInput
{
    uint3 dispatchThreadId : SV_DispatchThreadID;
    uint3 groupID : SV_GroupID;
    uint3 groupThreadID : SV_GroupThreadID;
};

uint GetBaseInstanceLookup(uint drawCallID)
{
    uint byteOffset = (_constants.drawCallDataSize * drawCallID) + _constants.baseInstanceLookupOffset;
    uint baseInstanceLookup = _drawCallDatas.Load(byteOffset);
    return baseInstanceLookup;
}

[shader("compute")]
[numthreads(32, 1, 1)]
void main(CSInput input)
{
    uint instanceRefID = input.dispatchThreadId.x;

    if (instanceRefID >= _constants.numTotalInstances)
        return;
    uint bitMask = _culledDrawCallsBitMask[input.groupID.x];
    uint bitIndex = input.groupThreadID.x;

    if (bitMask & (1u << bitIndex))
    {
        InstanceRef instanceRef = _instanceRefTable[instanceRefID];

        uint countByteOffset = instanceRef.drawID * sizeof(uint);

        uint instanceIndex;
        _culledInstanceCounts.InterlockedAdd(countByteOffset, 1, instanceIndex);

        uint baseInstanceLookup = GetBaseInstanceLookup(instanceRef.drawID);
        uint instanceLookupOffset = baseInstanceLookup + instanceIndex;
        _culledInstanceLookupTable[instanceLookupOffset] = instanceRefID;
    }
}