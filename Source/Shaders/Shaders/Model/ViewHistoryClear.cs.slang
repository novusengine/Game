struct Constants
{
    uint offset;
    uint count;
    uint reserved;
};
[[vk::push_constant]] Constants _constants;

[[vk::binding(0, PER_PASS)]] RWStructuredBuffer<uint> _clearMeshletHistory0;
[[vk::binding(1, PER_PASS)]] RWStructuredBuffer<uint> _clearMeshletHistory1;

[numthreads(64, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    if (dispatchThreadID.x >= _constants.count)
        return;

    const uint index = _constants.offset + dispatchThreadID.x;
    _clearMeshletHistory0[index] = 0u;
    _clearMeshletHistory1[index] = 0u;
}
