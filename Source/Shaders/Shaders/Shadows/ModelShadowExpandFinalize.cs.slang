#include "DescriptorSet/Global.inc.slang"
#include "Shadows/ModelShadow.inc.slang"

struct Constants
{
    uint queueCapacity;
    uint frameIndex;
};
[[vk::push_constant]] Constants _constants;

[[vk::binding(0, PER_PASS)]] RWStructuredBuffer<uint> _shadowExpandStats0;
[[vk::binding(1, PER_PASS)]] RWStructuredBuffer<uint> _shadowChunkArguments0;
[[vk::binding(2, PER_PASS)]] RWStructuredBuffer<uint> _shadowExpandStats1;
[[vk::binding(3, PER_PASS)]] RWStructuredBuffer<uint> _shadowChunkArguments1;

[shader("compute")]
[numthreads(1, 1, 1)]
void main()
{
    uint count = _constants.frameIndex == 0u ? _shadowExpandStats0[MODEL_SHADOW_STAT_EXPANDED_CHUNKS] :
                                               _shadowExpandStats1[MODEL_SHADOW_STAT_EXPANDED_CHUNKS];
    count = min(count, _constants.queueCapacity);
    uint groupsX = min(count, MODEL_SHADOW_COMPUTE_DISPATCH_MAX_GROUPS_X);
    uint groupsY = count == 0u ? 0u : (count + groupsX - 1u) / groupsX;
    if (_constants.frameIndex == 0u)
    {
        _shadowChunkArguments0[0] = groupsX;
        _shadowChunkArguments0[1] = groupsY;
        _shadowChunkArguments0[2] = count == 0u ? 0u : 1u;
    }
    else
    {
        _shadowChunkArguments1[0] = groupsX;
        _shadowChunkArguments1[1] = groupsY;
        _shadowChunkArguments1[2] = count == 0u ? 0u : 1u;
    }
}
