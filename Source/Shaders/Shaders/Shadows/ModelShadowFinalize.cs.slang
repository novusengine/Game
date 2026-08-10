#include "DescriptorSet/Global.inc.slang"
#include "Shadows/ModelShadow.inc.slang"

struct Constants
{
    uint queueCapacity;
    uint frameIndex;
};
[[vk::push_constant]] Constants _constants;

[[vk::binding(0, PER_PASS)]] RWStructuredBuffer<uint> _shadowFinalizeStats0;
[[vk::binding(1, PER_PASS)]] RWStructuredBuffer<uint> _shadowArguments0;
[[vk::binding(2, PER_PASS)]] RWStructuredBuffer<uint> _shadowFinalizeStats1;
[[vk::binding(3, PER_PASS)]] RWStructuredBuffer<uint> _shadowArguments1;

[shader("compute")]
[numthreads(1, 1, 1)]
void main()
{
    for (uint queueIndex = 0u; queueIndex < MODEL_SHADOW_QUEUE_COUNT; ++queueIndex)
    {
        uint count = _constants.frameIndex == 0u ?
            _shadowFinalizeStats0[MODEL_SHADOW_STAT_QUEUE_COUNTS_BASE + queueIndex] :
            _shadowFinalizeStats1[MODEL_SHADOW_STAT_QUEUE_COUNTS_BASE + queueIndex];
        count = min(count, _constants.queueCapacity);
        uint offset = queueIndex * MODEL_SHADOW_DISPATCH_ARGUMENT_COUNT;
        uint x = min(count, MODEL_SHADOW_MESH_DISPATCH_MAX_GROUPS_X);
        uint y = count == 0u ? 0u : (count + MODEL_SHADOW_MESH_DISPATCH_MAX_GROUPS_X - 1u) /
                                         MODEL_SHADOW_MESH_DISPATCH_MAX_GROUPS_X;
        if (_constants.frameIndex == 0u)
        {
            _shadowArguments0[offset] = x;
            _shadowArguments0[offset + 1u] = y;
            _shadowArguments0[offset + 2u] = count == 0u ? 0u : 1u;
        }
        else
        {
            _shadowArguments1[offset] = x;
            _shadowArguments1[offset + 1u] = y;
            _shadowArguments1[offset + 2u] = count == 0u ? 0u : 1u;
        }
    }
}
