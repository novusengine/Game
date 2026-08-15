#include "Model/ModelTransparentWork.inc.slang"

struct Constants
{
    uint resourceIndex;
};
[[vk::push_constant]] Constants _constants;

[[vk::binding(0, PER_PASS)]] RWStructuredBuffer<uint> _transparentStats0;
[[vk::binding(1, PER_PASS)]] RWStructuredBuffer<uint> _transparentStats1;
[[vk::binding(2, PER_PASS)]] RWStructuredBuffer<uint> _transparentChunkArguments0;
[[vk::binding(3, PER_PASS)]] RWStructuredBuffer<uint> _transparentChunkArguments1;
[[vk::binding(4, PER_PASS)]] RWStructuredBuffer<uint> _transparentArguments0;
[[vk::binding(5, PER_PASS)]] RWStructuredBuffer<uint> _transparentArguments1;

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint index = dispatchThreadID.x;
    if (index < MODEL_TRANSPARENT_STAT_COUNT)
    {
        if (_constants.resourceIndex == 0u) _transparentStats0[index] = 0u;
        else _transparentStats1[index] = 0u;
    }
    if (index < MODEL_TRANSPARENT_DISPATCH_ARGUMENT_COUNT)
    {
        if (_constants.resourceIndex == 0u) _transparentChunkArguments0[index] = 0u;
        else _transparentChunkArguments1[index] = 0u;
    }
    if (index < MODEL_TRANSPARENT_BIN_COUNT * MODEL_TRANSPARENT_DISPATCH_ARGUMENT_COUNT)
    {
        if (_constants.resourceIndex == 0u) _transparentArguments0[index] = 0u;
        else _transparentArguments1[index] = 0u;
    }
}
