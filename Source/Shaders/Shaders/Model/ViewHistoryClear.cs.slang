struct Constants
{
    uint mode;
    uint offset;
    uint count;
};
[[vk::push_constant]] Constants _constants;

[[vk::binding(0, PER_PASS)]] RWStructuredBuffer<uint> _clearInstanceVisibility0;
[[vk::binding(1, PER_PASS)]] RWStructuredBuffer<uint> _clearInstanceVisibility1;
[[vk::binding(2, PER_PASS)]] RWStructuredBuffer<uint> _clearMeshletHistory0;
[[vk::binding(3, PER_PASS)]] RWStructuredBuffer<uint> _clearMeshletHistory1;

[numthreads(64, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    if (dispatchThreadID.x >= _constants.count)
        return;

    const uint index = _constants.offset + dispatchThreadID.x;
    if (_constants.mode == 0u)
    {
        const uint wordIndex = index >> 5u;
        const uint keepMask = ~(1u << (index & 31u));
        InterlockedAnd(_clearInstanceVisibility0[wordIndex], keepMask);
        InterlockedAnd(_clearInstanceVisibility1[wordIndex], keepMask);
    }
    else
    {
        _clearMeshletHistory0[index] = 0u;
        _clearMeshletHistory1[index] = 0u;
    }
}
