#include "Model/ModelTransparentWork.inc.slang"

struct Constants
{
    uint chunkCapacity;
    uint resourceIndex;
};
[[vk::push_constant]] Constants _constants;

[[vk::binding(0, PER_PASS)]] StructuredBuffer<uint> _transparentStats0;
[[vk::binding(1, PER_PASS)]] StructuredBuffer<uint> _transparentStats1;
[[vk::binding(2, PER_PASS)]] RWStructuredBuffer<uint> _transparentChunkArguments0;
[[vk::binding(3, PER_PASS)]] RWStructuredBuffer<uint> _transparentChunkArguments1;

[shader("compute")]
[numthreads(1, 1, 1)]
void main()
{
    uint count = min(_constants.resourceIndex == 0u ? _transparentStats0[MODEL_TRANSPARENT_STAT_EXPANDED_CHUNKS]
                                                    : _transparentStats1[MODEL_TRANSPARENT_STAT_EXPANDED_CHUNKS],
                     _constants.chunkCapacity);
    if (_constants.resourceIndex == 0u)
    {
        _transparentChunkArguments0[0] = count;
        _transparentChunkArguments0[1] = count == 0u ? 0u : 1u;
        _transparentChunkArguments0[2] = count == 0u ? 0u : 1u;
    }
    else
    {
        _transparentChunkArguments1[0] = count;
        _transparentChunkArguments1[1] = count == 0u ? 0u : 1u;
        _transparentChunkArguments1[2] = count == 0u ? 0u : 1u;
    }
}
