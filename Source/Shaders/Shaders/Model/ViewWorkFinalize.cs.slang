#include "Model/ModelViewWork.inc.slang"

struct Constants
{
    uint viewIndex;
    uint inputCount;
    uint queueCapacity;
    int forcedLOD;
    float viewportHeight;
    float lodTargetPixels;
    float lodHysteresis;
    uint resetHistory;
    uint resourceIndex;
};
[[vk::push_constant]] Constants _constants;

[[vk::binding(14, PER_PASS)]] RWStructuredBuffer<uint> _workStats0;
[[vk::binding(15, PER_PASS)]] RWStructuredBuffer<uint> _indirectArguments0;
[[vk::binding(16, PER_PASS)]] RWStructuredBuffer<uint> _workStats1;
[[vk::binding(17, PER_PASS)]] RWStructuredBuffer<uint> _indirectArguments1;

[shader("compute")]
[numthreads(1, 1, 1)]
void main()
{
    uint oneSidedCount;
    uint twoSidedCount;
    if (_constants.resourceIndex == 0u)
    {
        _workStats0[MODEL_WORK_STAT_SELECTED_INSTANCES] = _constants.inputCount;
        _workStats0[MODEL_WORK_STAT_VISIBILITY_RECORDS] =
            min(_workStats0[MODEL_WORK_STAT_VISIBILITY_RECORDS], _constants.queueCapacity);
        oneSidedCount = min(_workStats0[MODEL_WORK_STAT_ONE_SIDED_MESHLETS], _constants.queueCapacity);
        twoSidedCount = min(_workStats0[MODEL_WORK_STAT_TWO_SIDED_MESHLETS], _constants.queueCapacity);
        _indirectArguments0[MODEL_ONE_SIDED_ARGUMENT_OFFSET + MODEL_DISPATCH_ARGUMENT_X] =
            min(oneSidedCount, MODEL_MESH_DISPATCH_MAX_GROUPS_X);
        _indirectArguments0[MODEL_ONE_SIDED_ARGUMENT_OFFSET + MODEL_DISPATCH_ARGUMENT_Y] =
            oneSidedCount == 0u ? 0u : (oneSidedCount + MODEL_MESH_DISPATCH_MAX_GROUPS_X - 1u) /
                                              MODEL_MESH_DISPATCH_MAX_GROUPS_X;
        _indirectArguments0[MODEL_ONE_SIDED_ARGUMENT_OFFSET + MODEL_DISPATCH_ARGUMENT_Z] =
            oneSidedCount == 0u ? 0u : 1u;
        _indirectArguments0[MODEL_TWO_SIDED_ARGUMENT_OFFSET + MODEL_DISPATCH_ARGUMENT_X] =
            min(twoSidedCount, MODEL_MESH_DISPATCH_MAX_GROUPS_X);
        _indirectArguments0[MODEL_TWO_SIDED_ARGUMENT_OFFSET + MODEL_DISPATCH_ARGUMENT_Y] =
            twoSidedCount == 0u ? 0u : (twoSidedCount + MODEL_MESH_DISPATCH_MAX_GROUPS_X - 1u) /
                                              MODEL_MESH_DISPATCH_MAX_GROUPS_X;
        _indirectArguments0[MODEL_TWO_SIDED_ARGUMENT_OFFSET + MODEL_DISPATCH_ARGUMENT_Z] =
            twoSidedCount == 0u ? 0u : 1u;
    }
    else
    {
        _workStats1[MODEL_WORK_STAT_SELECTED_INSTANCES] = _constants.inputCount;
        _workStats1[MODEL_WORK_STAT_VISIBILITY_RECORDS] =
            min(_workStats1[MODEL_WORK_STAT_VISIBILITY_RECORDS], _constants.queueCapacity);
        oneSidedCount = min(_workStats1[MODEL_WORK_STAT_ONE_SIDED_MESHLETS], _constants.queueCapacity);
        twoSidedCount = min(_workStats1[MODEL_WORK_STAT_TWO_SIDED_MESHLETS], _constants.queueCapacity);
        _indirectArguments1[MODEL_ONE_SIDED_ARGUMENT_OFFSET + MODEL_DISPATCH_ARGUMENT_X] =
            min(oneSidedCount, MODEL_MESH_DISPATCH_MAX_GROUPS_X);
        _indirectArguments1[MODEL_ONE_SIDED_ARGUMENT_OFFSET + MODEL_DISPATCH_ARGUMENT_Y] =
            oneSidedCount == 0u ? 0u : (oneSidedCount + MODEL_MESH_DISPATCH_MAX_GROUPS_X - 1u) /
                                              MODEL_MESH_DISPATCH_MAX_GROUPS_X;
        _indirectArguments1[MODEL_ONE_SIDED_ARGUMENT_OFFSET + MODEL_DISPATCH_ARGUMENT_Z] =
            oneSidedCount == 0u ? 0u : 1u;
        _indirectArguments1[MODEL_TWO_SIDED_ARGUMENT_OFFSET + MODEL_DISPATCH_ARGUMENT_X] =
            min(twoSidedCount, MODEL_MESH_DISPATCH_MAX_GROUPS_X);
        _indirectArguments1[MODEL_TWO_SIDED_ARGUMENT_OFFSET + MODEL_DISPATCH_ARGUMENT_Y] =
            twoSidedCount == 0u ? 0u : (twoSidedCount + MODEL_MESH_DISPATCH_MAX_GROUPS_X - 1u) /
                                              MODEL_MESH_DISPATCH_MAX_GROUPS_X;
        _indirectArguments1[MODEL_TWO_SIDED_ARGUMENT_OFFSET + MODEL_DISPATCH_ARGUMENT_Z] =
            twoSidedCount == 0u ? 0u : 1u;
    }
}
