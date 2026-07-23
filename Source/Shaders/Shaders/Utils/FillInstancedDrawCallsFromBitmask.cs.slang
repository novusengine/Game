permutation IS_INDEXED = [0, 1];
permutation DYNAMIC_FILTER = [0, 1]; // 1 adds the per-instance dynamic mask filter for the SVSM caster split

#include "DescriptorSet/Global.inc.slang"

#include "Include/Common.inc.slang"
#include "Include/Culling.inc.slang"
#include "Include/PyramidCulling.inc.slang"

struct Constants
{
    uint numTotalInstances;
    uint baseInstanceLookupOffset; // Byte offset into drawCallDatas where the baseInstanceLookup is stored
    uint drawCallDataSize;
    uint currentBitmaskIndex;
    uint bitmaskOffset; // Selects which view's bitmask slice to fill from
    uint keepDynamic;   // DYNAMIC_FILTER only: 0 = keep static instances, 1 = keep dynamic instances
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

// Both bitmask generations stay bound; currentBitmaskIndex selects which one is current this frame
[[vk::binding(0, PER_PASS)]] StructuredBuffer<InstanceRef> _instanceRefTable;
[[vk::binding(1, PER_PASS)]] StructuredBuffer<uint> _culledDrawCallsBitMask0;
[[vk::binding(2, PER_PASS)]] ByteAddressBuffer _drawCallDatas;
[[vk::binding(5, PER_PASS)]] StructuredBuffer<uint> _culledDrawCallsBitMask1;

[[vk::binding(3, PER_PASS)]] RWByteAddressBuffer _culledInstanceCounts; // One uint per draw call
[[vk::binding(4, PER_PASS)]] RWStructuredBuffer<uint> _culledInstanceLookupTable; // One uint per instance, contains instanceRefID of what survives culling, and thus can get reordered

#if DYNAMIC_FILTER
[[vk::binding(6, PER_PASS)]] StructuredBuffer<uint> _dynamicInstanceMask; // One bit per instanceID, rebuilt every frame
#endif

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
    uint bitMask;
    if (_constants.currentBitmaskIndex == 0)
    {
        bitMask = _culledDrawCallsBitMask0[input.groupID.x + _constants.bitmaskOffset];
    }
    else
    {
        bitMask = _culledDrawCallsBitMask1[input.groupID.x + _constants.bitmaskOffset];
    }
    uint bitIndex = input.groupThreadID.x;

    if (bitMask & (1u << bitIndex))
    {
        InstanceRef instanceRef = _instanceRefTable[instanceRefID];

#if DYNAMIC_FILTER
        // SVSM caster split: keep only the requested class
        bool isDynamic = (_dynamicInstanceMask[instanceRef.instanceID / 32] & (1u << (instanceRef.instanceID & 31))) != 0;
        if (isDynamic != (_constants.keepDynamic != 0))
            return;
#endif

        uint countByteOffset = instanceRef.drawID * sizeof(uint);

        uint instanceIndex;
        _culledInstanceCounts.InterlockedAdd(countByteOffset, 1, instanceIndex);

        uint baseInstanceLookup = GetBaseInstanceLookup(instanceRef.drawID);
        uint instanceLookupOffset = baseInstanceLookup + instanceIndex;
        _culledInstanceLookupTable[instanceLookupOffset] = instanceRefID;
    }
}