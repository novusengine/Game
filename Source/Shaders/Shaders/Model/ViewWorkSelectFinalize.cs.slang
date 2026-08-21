#include "DescriptorSet/Global.inc.slang"
#include "Model/ModelViewWork.inc.slang"

struct Constants
{
    uint selectionCapacity;
    uint resourceIndex;
};
[[vk::push_constant]] Constants _constants;

[[vk::binding(0, PER_PASS)]] RWStructuredBuffer<uint> _workStats0;
[[vk::binding(1, PER_PASS)]] RWStructuredBuffer<uint> _selectionArguments0;
[[vk::binding(2, PER_PASS)]] RWStructuredBuffer<uint> _workStats1;
[[vk::binding(3, PER_PASS)]] RWStructuredBuffer<uint> _selectionArguments1;

[shader("compute")]
[numthreads(1, 1, 1)]
void main()
{
    uint count = min(_constants.resourceIndex == 0u
                         ? _workStats0[MODEL_WORK_STAT_SELECTED_INSTANCES]
                         : _workStats1[MODEL_WORK_STAT_SELECTED_INSTANCES],
                     _constants.selectionCapacity);
    if (_constants.resourceIndex == 0u)
    {
        _selectionArguments0[0] = count;
        _selectionArguments0[1] = count == 0u ? 0u : 1u;
        _selectionArguments0[2] = count == 0u ? 0u : 1u;
    }
    else
    {
        _selectionArguments1[0] = count;
        _selectionArguments1[1] = count == 0u ? 0u : 1u;
        _selectionArguments1[2] = count == 0u ? 0u : 1u;
    }
}
