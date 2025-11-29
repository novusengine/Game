
struct IndirectArguments
{
    uint indexCount;
    uint instanceCount;
    uint firstIndex;
    int vertexOffset;
    uint firstInstance;
};

struct Constants
{
    uint moveCountToFirst;
};

[[vk::push_constant]] Constants _constants;

[[vk::binding(0, PER_PASS)]] RWStructuredBuffer<IndirectArguments> _arguments;

[shader("compute")]
[numthreads(1, 1, 1)]
void main()
{
    IndirectArguments arguments = _arguments[0];

    // This lets us do several partial draws with the same instance buffers
    if (_constants.moveCountToFirst)
    {
        arguments.firstInstance = arguments.instanceCount;
    }

    arguments.instanceCount = 0;
    _arguments[0] = arguments;
}