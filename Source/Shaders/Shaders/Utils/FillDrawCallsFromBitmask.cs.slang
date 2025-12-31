permutation IS_INDEXED = [0, 1];

#include "DescriptorSet/Global.inc.slang"

#include "Include/Common.inc.slang"
#include "Include/Culling.inc.slang"
#include "Include/PyramidCulling.inc.slang"

struct Constants
{
    uint numTotalDraws;
    uint bitmaskOffset;
    uint diffAgainstPrev;
};

#if IS_INDEXED
typedef IndexedDraw DrawType;
#else
typedef Draw DrawType;
#endif

[[vk::push_constant]] Constants _constants;

[[vk::binding(0, PER_PASS)]] StructuredBuffer<DrawType> _drawCalls;
[[vk::binding(1, PER_PASS)]] StructuredBuffer<uint> _culledDrawCallsBitMask;
[[vk::binding(2, PER_PASS)]] StructuredBuffer<uint> _prevCulledDrawCallsBitMask;

[[vk::binding(3, PER_PASS)]] RWStructuredBuffer<DrawType> _culledDrawCalls;
[[vk::binding(4, PER_PASS)]] RWByteAddressBuffer _drawCount;
[[vk::binding(5, PER_PASS)]] RWByteAddressBuffer _triangleCount;

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

    if (index >= _constants.numTotalDraws)
        return;

    uint bitMaskIndex = _constants.bitmaskOffset + input.groupID.x;
    uint bitMask = _culledDrawCallsBitMask[bitMaskIndex];
    uint prevBitMask = _prevCulledDrawCallsBitMask[bitMaskIndex];
    uint bitIndex = input.groupThreadID.x;

    bool isVisible = bitMask & (1u << bitIndex);
    bool wasAlreadyDrawn = prevBitMask & (1u << bitIndex);

    bool shouldDraw = isVisible && (!wasAlreadyDrawn || !_constants.diffAgainstPrev);
    if (shouldDraw)
    {
        DrawType drawCall = _drawCalls[index];

        if (drawCall.instanceCount == 0)
            return;

#if IS_INDEXED
        uint triangleCount = drawCall.instanceCount * (drawCall.indexCount / 3);
#else
        uint triangleCount = drawCall.instanceCount * (drawCall.vertexCount / 3);
#endif

        uint outTriangles;
        _triangleCount.InterlockedAdd(0, triangleCount, outTriangles);

        uint outIndex;
        _drawCount.InterlockedAdd(0, drawCall.instanceCount, outIndex);

        _culledDrawCalls[outIndex] = drawCall;
    }
}