#include "Model/ModelTransparentWork.inc.slang"

struct Constants
{
    uint resourceIndex;
    uint binCapacity;
};
[[vk::push_constant]] Constants _constants;

[[vk::binding(0, PER_PASS)]] StructuredBuffer<uint> _transparentStats0;
[[vk::binding(1, PER_PASS)]] StructuredBuffer<uint> _transparentStats1;
[[vk::binding(2, PER_PASS)]] RWStructuredBuffer<uint> _transparentArguments0;
[[vk::binding(3, PER_PASS)]] RWStructuredBuffer<uint> _transparentArguments1;

[shader("compute")]
[numthreads(32, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint binIndex = dispatchThreadID.x;
    if (binIndex >= MODEL_TRANSPARENT_BIN_COUNT)
        return;
    uint count = min(_constants.resourceIndex == 0u ? _transparentStats0[MODEL_TRANSPARENT_STAT_QUEUE_COUNTS + binIndex]
                                                    : _transparentStats1[MODEL_TRANSPARENT_STAT_QUEUE_COUNTS + binIndex],
                     _constants.binCapacity);
    uint offset = binIndex * MODEL_TRANSPARENT_DISPATCH_ARGUMENT_COUNT;
    if (_constants.resourceIndex == 0u)
    {
        _transparentArguments0[offset] = min(count, MODEL_MESH_DISPATCH_MAX_GROUPS_X);
        _transparentArguments0[offset + 1u] = count == 0u ? 0u : (count + MODEL_MESH_DISPATCH_MAX_GROUPS_X - 1u) / MODEL_MESH_DISPATCH_MAX_GROUPS_X;
        _transparentArguments0[offset + 2u] = count == 0u ? 0u : 1u;
    }
    else
    {
        _transparentArguments1[offset] = min(count, MODEL_MESH_DISPATCH_MAX_GROUPS_X);
        _transparentArguments1[offset + 1u] = count == 0u ? 0u : (count + MODEL_MESH_DISPATCH_MAX_GROUPS_X - 1u) / MODEL_MESH_DISPATCH_MAX_GROUPS_X;
        _transparentArguments1[offset + 2u] = count == 0u ? 0u : 1u;
    }
}
