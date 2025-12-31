permutation IS_INDEXED = [0, 1];
permutation DEBUG_ORDERED = [0, 1];

#include "DescriptorSet/Global.inc.slang"

#include "Include/Common.inc.slang"
#include "Include/Culling.inc.slang"
#include "Include/Debug.inc.slang"
#include "Include/PyramidCulling.inc.slang"

struct Constants
{
    uint numTotalDrawCalls;
    uint baseInstanceLookupOffset;
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

// Inputs
[[vk::push_constant]] Constants _constants;
[[vk::binding(0, PER_PASS)]] StructuredBuffer<DrawType> _drawCalls;
[[vk::binding(1, PER_PASS)]] ByteAddressBuffer _drawCallDatas;
[[vk::binding(2, PER_PASS)]] StructuredBuffer<uint> _culledInstanceCounts; // One uint per draw call

[[vk::binding(3, PER_PASS)]] RWStructuredBuffer<DrawType> _culledDrawCalls;
[[vk::binding(4, PER_PASS)]] RWByteAddressBuffer _culledDrawCallCount;
[[vk::binding(5, PER_PASS)]] RWByteAddressBuffer _drawCount;
[[vk::binding(6, PER_PASS)]] RWByteAddressBuffer _triangleCount;

uint GetBaseInstanceLookup(uint drawCallID)
{
    uint byteOffset = (_constants.drawCallDataSize * drawCallID) + _constants.baseInstanceLookupOffset;
    uint baseInstanceLookup = _drawCallDatas.Load(byteOffset);
    return baseInstanceLookup;
}

struct CSInput
{
    uint3 dispatchThreadID : SV_DispatchThreadID;
};

// One thread per drawcall
[shader("compute")]
#if DEBUG_ORDERED
[numthreads(1, 1, 1)]
#else
[numthreads(32, 1, 1)]
#endif
void main(CSInput input)
{
#if !DEBUG_ORDERED
    // Load DrawCall
    uint drawCallID = input.dispatchThreadID.x;
    if (drawCallID >= _constants.numTotalDrawCalls)
    {
        return;
    }
#else
    for (uint drawCallID = 0; drawCallID < _constants.numTotalDrawCalls; drawCallID++)
    {
#endif

        DrawType drawCall = _drawCalls[drawCallID];

        // Load instanceCount and baseInstanceLookup
        uint instanceCount = _culledInstanceCounts[drawCallID];
        if (instanceCount == 0)
        {
#if DEBUG_ORDERED
            continue;
#else
            return;
#endif
        }

        uint baseInstanceLookup = GetBaseInstanceLookup(drawCallID);

        // Point them towards the correct instances
        drawCall.instanceCount = instanceCount;
        drawCall.firstInstance = baseInstanceLookup;

        // Reserve a drawcall slot
        uint culledDrawCallID;
        _culledDrawCallCount.InterlockedAdd(0, 1, culledDrawCallID);

        // Output to that slot
        _culledDrawCalls[culledDrawCallID] = drawCall;

        // Update stats
        uint unused;
        _drawCount.InterlockedAdd(0, instanceCount, unused);

#if IS_INDEXED
        uint triangleCount = instanceCount * (drawCall.indexCount / 3);
#else
        uint triangleCount = instanceCount * (drawCall.vertexCount / 3);
#endif
        _triangleCount.InterlockedAdd(0, triangleCount, unused);

#if DEBUG_ORDERED
    }
#endif
}