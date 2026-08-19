#include "Material/MaterialABI.inc.slang"

#define MATERIAL_DISPATCH_ARGUMENT_COUNT 3u
#define MATERIAL_RESOLVE_MAX_GROUPS_X 1024u

struct Constants
{
    uint resourceIndex;
    uint tileCapacity;
};
[[vk::push_constant]] Constants _constants;

[[vk::binding(23, PER_PASS)]] RWStructuredBuffer<uint> _materialCounters0;
[[vk::binding(24, PER_PASS)]] RWStructuredBuffer<uint> _materialCounters1;
[[vk::binding(25, PER_PASS)]] RWStructuredBuffer<uint> _materialArguments0;
[[vk::binding(26, PER_PASS)]] RWStructuredBuffer<uint> _materialArguments1;

[shader("compute")]
[numthreads(MATERIAL_EXECUTION_GROUP_COUNT, 1, 1)]
void main(uint3 threadID : SV_DispatchThreadID)
{
    uint group = threadID.x;
    uint count = _constants.resourceIndex == 0u ? _materialCounters0[group] : _materialCounters1[group];
    count = min(count, _constants.tileCapacity);
    uint offset = group * MATERIAL_DISPATCH_ARGUMENT_COUNT;
    if (_constants.resourceIndex == 0u)
    {
        uint groupsX = min(count, MATERIAL_RESOLVE_MAX_GROUPS_X);
        _materialArguments0[offset] = groupsX;
        _materialArguments0[offset + 1u] = count == 0u ? 0u : (count + groupsX - 1u) / groupsX;
        _materialArguments0[offset + 2u] = count == 0u ? 0u : 1u;
    }
    else
    {
        uint groupsX = min(count, MATERIAL_RESOLVE_MAX_GROUPS_X);
        _materialArguments1[offset] = groupsX;
        _materialArguments1[offset + 1u] = count == 0u ? 0u : (count + groupsX - 1u) / groupsX;
        _materialArguments1[offset + 2u] = count == 0u ? 0u : 1u;
    }
}
