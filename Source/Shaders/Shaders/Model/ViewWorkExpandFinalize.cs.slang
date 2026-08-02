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
    if (_constants.resourceIndex == 0u)
    {
        _chunkArguments0[MODEL_DISPATCH_ARGUMENT_X] = count;
        _chunkArguments0[MODEL_DISPATCH_ARGUMENT_Y] = count == 0u ? 0u : 1u;
        _chunkArguments0[MODEL_DISPATCH_ARGUMENT_Z] = count == 0u ? 0u : 1u;
    }
    else
    {
        _chunkArguments1[MODEL_DISPATCH_ARGUMENT_X] = count;
        _chunkArguments1[MODEL_DISPATCH_ARGUMENT_Y] = count == 0u ? 0u : 1u;
        _chunkArguments1[MODEL_DISPATCH_ARGUMENT_Z] = count == 0u ? 0u : 1u;
    }
}
