struct Constants
{
    uint numModels;
};
[[vk::push_constant]] Constants _constants;

struct Meshlet
{
    uint indexStart;
    uint indexCount;
};

struct InstanceData
{
    uint meshletOffset;
    uint meshletCount;
    uint indexOffset;
    uint padding;
};

[[vk::binding(0, PER_DRAW)]] StructuredBuffer<uint> _indices;
[[vk::binding(1, PER_DRAW)]] StructuredBuffer<Meshlet> _meshlets;
[[vk::binding(2, PER_DRAW)]] StructuredBuffer<InstanceData> _instanceDatas;

[[vk::binding(3, PER_DRAW)]] RWStructuredBuffer<uint> _culledIndices;
[[vk::binding(4, PER_DRAW)]] RWByteAddressBuffer _arguments;

struct CSInput
{
    int3 dispatchThreadID : SV_DispatchThreadID;
};

[numthreads(32, 1, 1)]
void main(CSInput input)
{
    if (input.dispatchThreadID.x >= _constants.numModels)
        return;

    InstanceData instanceData = _instanceDatas[input.dispatchThreadID.x];
    
    // Do Per Instance Culling Here
    { }
    
    for (uint i = 0; i < instanceData.meshletCount; i++)
    {
        Meshlet meshlet = _meshlets[instanceData.meshletOffset + i];
        
        // Do Per Meshlet Culling Here
        { }
        
        uint offset;
        _arguments.InterlockedAdd(0, meshlet.indexCount, offset);
        
        for (uint j = 0; j < meshlet.indexCount; j++)
        {
            uint meshletGlobalIndex = instanceData.indexOffset + meshlet.indexStart + j;
            
            _culledIndices[offset + j] = _indices[meshletGlobalIndex];
        }
    }
}