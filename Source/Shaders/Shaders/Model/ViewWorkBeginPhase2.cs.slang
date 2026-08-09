#include "DescriptorSet/Global.inc.slang"
#include "Model/ModelViewWork.inc.slang"

struct Constants
{
    uint resourceIndex;
};
[[vk::push_constant]] Constants _constants;

[[vk::binding(0, PER_PASS)]] RWStructuredBuffer<uint> _workStats0;
[[vk::binding(1, PER_PASS)]] RWStructuredBuffer<uint> _workStats1;
[[vk::binding(2, PER_PASS)]] RWStructuredBuffer<uint> _indirectArguments0;
[[vk::binding(3, PER_PASS)]] RWStructuredBuffer<uint> _indirectArguments1;

[shader("compute")]
[numthreads(1, 1, 1)]
void main()
{
    for (uint rasterClass = 0u; rasterClass < MODEL_RASTER_CLASS_COUNT; ++rasterClass)
    {
        uint argumentOffset = rasterClass * MODEL_MESH_DISPATCH_ARGUMENT_COUNT;
        if (_constants.resourceIndex == 0u)
        {
            _workStats0[MODEL_WORK_STAT_RASTER_MESHLETS_BASE + rasterClass] = 0u;
            _workStats0[MODEL_WORK_STAT_COMMITTED_RASTER_MESHLETS_BASE + rasterClass] = 0u;
            _indirectArguments0[argumentOffset + 0u] = 0u;
            _indirectArguments0[argumentOffset + 1u] = 0u;
            _indirectArguments0[argumentOffset + 2u] = 0u;
        }
        else
        {
            _workStats1[MODEL_WORK_STAT_RASTER_MESHLETS_BASE + rasterClass] = 0u;
            _workStats1[MODEL_WORK_STAT_COMMITTED_RASTER_MESHLETS_BASE + rasterClass] = 0u;
            _indirectArguments1[argumentOffset + 0u] = 0u;
            _indirectArguments1[argumentOffset + 1u] = 0u;
            _indirectArguments1[argumentOffset + 2u] = 0u;
        }
    }
}
