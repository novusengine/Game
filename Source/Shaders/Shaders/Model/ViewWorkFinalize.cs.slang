#include "DescriptorSet/Global.inc.slang"
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
    if (_constants.resourceIndex == 0u)
    {
        _workStats0[MODEL_WORK_STAT_SELECTED_INSTANCES] = _constants.inputCount;
        _workStats0[MODEL_WORK_STAT_VISIBILITY_RECORDS] =
            min(_workStats0[MODEL_WORK_STAT_VISIBILITY_RECORDS], _constants.queueCapacity);
        for (uint rasterClass = 0u; rasterClass < MODEL_RASTER_CLASS_COUNT; ++rasterClass)
        {
            uint count = min(_workStats0[MODEL_WORK_STAT_RASTER_MESHLETS_BASE + rasterClass], _constants.queueCapacity);
            uint offset = rasterClass * MODEL_MESH_DISPATCH_ARGUMENT_COUNT;
            _indirectArguments0[offset + MODEL_DISPATCH_ARGUMENT_X] = min(count, MODEL_MESH_DISPATCH_MAX_GROUPS_X);
            _indirectArguments0[offset + MODEL_DISPATCH_ARGUMENT_Y] = count == 0u ? 0u :
                (count + MODEL_MESH_DISPATCH_MAX_GROUPS_X - 1u) / MODEL_MESH_DISPATCH_MAX_GROUPS_X;
            _indirectArguments0[offset + MODEL_DISPATCH_ARGUMENT_Z] = count == 0u ? 0u : 1u;
        }
    }
    else
    {
        _workStats1[MODEL_WORK_STAT_SELECTED_INSTANCES] = _constants.inputCount;
        _workStats1[MODEL_WORK_STAT_VISIBILITY_RECORDS] =
            min(_workStats1[MODEL_WORK_STAT_VISIBILITY_RECORDS], _constants.queueCapacity);
        for (uint rasterClass = 0u; rasterClass < MODEL_RASTER_CLASS_COUNT; ++rasterClass)
        {
            uint count = min(_workStats1[MODEL_WORK_STAT_RASTER_MESHLETS_BASE + rasterClass], _constants.queueCapacity);
            uint offset = rasterClass * MODEL_MESH_DISPATCH_ARGUMENT_COUNT;
            _indirectArguments1[offset + MODEL_DISPATCH_ARGUMENT_X] = min(count, MODEL_MESH_DISPATCH_MAX_GROUPS_X);
            _indirectArguments1[offset + MODEL_DISPATCH_ARGUMENT_Y] = count == 0u ? 0u :
                (count + MODEL_MESH_DISPATCH_MAX_GROUPS_X - 1u) / MODEL_MESH_DISPATCH_MAX_GROUPS_X;
            _indirectArguments1[offset + MODEL_DISPATCH_ARGUMENT_Z] = count == 0u ? 0u : 1u;
        }
    }
}
