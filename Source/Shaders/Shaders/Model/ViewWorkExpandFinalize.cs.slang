#include "DescriptorSet/Global.inc.slang"
#include "Model/ModelViewWork.inc.slang"

struct Constants
{
    uint queueCapacity;
    uint resourceIndex;
};
[[vk::push_constant]] Constants _constants;

[[vk::binding(0, PER_PASS)]] RWStructuredBuffer<uint> _workStats0;
[[vk::binding(1, PER_PASS)]] RWStructuredBuffer<uint> _chunkArguments0;
[[vk::binding(2, PER_PASS)]] RWStructuredBuffer<uint> _workStats1;
[[vk::binding(3, PER_PASS)]] RWStructuredBuffer<uint> _chunkArguments1;

[shader("compute")]
[numthreads(1, 1, 1)]
void main()
{
    uint count = min(_constants.resourceIndex == 0u
                         ? _workStats0[MODEL_WORK_STAT_EXPANDED_CHUNKS]
                         : _workStats1[MODEL_WORK_STAT_EXPANDED_CHUNKS],
                     _constants.queueCapacity);
    uint groupsX = count <= MODEL_COMPUTE_DISPATCH_LINEAR_THRESHOLD ? count : min(count, MODEL_COMPUTE_DISPATCH_MAX_GROUPS_X);
    uint groupsY = count == 0u ? 0u : (count + groupsX - 1u) / groupsX;
    if (_constants.resourceIndex == 0u)
    {
        _chunkArguments0[MODEL_DISPATCH_ARGUMENT_X] = groupsX;
        _chunkArguments0[MODEL_DISPATCH_ARGUMENT_Y] = groupsY;
        _chunkArguments0[MODEL_DISPATCH_ARGUMENT_Z] = count == 0u ? 0u : 1u;
    }
    else
    {
        _chunkArguments1[MODEL_DISPATCH_ARGUMENT_X] = groupsX;
        _chunkArguments1[MODEL_DISPATCH_ARGUMENT_Y] = groupsY;
        _chunkArguments1[MODEL_DISPATCH_ARGUMENT_Z] = count == 0u ? 0u : 1u;
    }
}
