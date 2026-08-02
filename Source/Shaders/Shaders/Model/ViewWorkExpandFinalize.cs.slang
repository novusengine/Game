struct Constants
{
    uint queueCapacity;
    uint resourceIndex;
};
[[vk::push_constant]] Constants _constants;

[[vk::binding(0, PER_PASS)]] RWStructuredBuffer<uint> _workStats0;
[[vk::binding(1, PER_PASS)]] RWStructuredBuffer<uint> _chunkArguments0;
[[vk::binding(2, PER_PASS)]] RWStructuredBuffer<uint> _workStats1;
[[vk::binding(3, PER_PASS)]] RWStructuredBuffer<uint> _chunkArguments1;

[shader("compute")]
[numthreads(1, 1, 1)]
void main()
{
    uint count = min(_constants.resourceIndex == 0u ? _workStats0[3] : _workStats1[3],
                     _constants.queueCapacity);
    if (_constants.resourceIndex == 0u)
    {
        _chunkArguments0[0] = count;
        _chunkArguments0[1] = count == 0u ? 0u : 1u;
        _chunkArguments0[2] = count == 0u ? 0u : 1u;
    }
    else
    {
        _chunkArguments1[0] = count;
        _chunkArguments1[1] = count == 0u ? 0u : 1u;
        _chunkArguments1[2] = count == 0u ? 0u : 1u;
    }
}
