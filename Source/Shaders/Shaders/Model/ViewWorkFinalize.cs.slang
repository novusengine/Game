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
        _workStats0[0] = _constants.inputCount;
        oneSidedCount = min(_workStats0[9], _constants.queueCapacity);
        twoSidedCount = min(_workStats0[10], _constants.queueCapacity);
        _indirectArguments0[0] = min(oneSidedCount, 1024u);
        _indirectArguments0[1] = oneSidedCount == 0u ? 0u : (oneSidedCount + 1023u) / 1024u;
        _indirectArguments0[2] = oneSidedCount == 0u ? 0u : 1u;
        _indirectArguments0[3] = min(twoSidedCount, 1024u);
        _indirectArguments0[4] = twoSidedCount == 0u ? 0u : (twoSidedCount + 1023u) / 1024u;
        _indirectArguments0[5] = twoSidedCount == 0u ? 0u : 1u;
    }
    else
    {
        _workStats1[0] = _constants.inputCount;
        oneSidedCount = min(_workStats1[9], _constants.queueCapacity);
        twoSidedCount = min(_workStats1[10], _constants.queueCapacity);
        _indirectArguments1[0] = min(oneSidedCount, 1024u);
        _indirectArguments1[1] = oneSidedCount == 0u ? 0u : (oneSidedCount + 1023u) / 1024u;
        _indirectArguments1[2] = oneSidedCount == 0u ? 0u : 1u;
        _indirectArguments1[3] = min(twoSidedCount, 1024u);
        _indirectArguments1[4] = twoSidedCount == 0u ? 0u : (twoSidedCount + 1023u) / 1024u;
        _indirectArguments1[5] = twoSidedCount == 0u ? 0u : 1u;
    }
}
