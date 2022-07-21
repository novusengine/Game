#include "Model/Shared.inc.hlsl"

struct Constants
{
    uint numMeshletsTotal;
    uint numMeshletsPerThread;
};
[[vk::push_constant]] Constants _constants;

[[vk::binding(0, PER_DRAW)]] StructuredBuffer<uint> _indices;
[[vk::binding(1, PER_DRAW)]] StructuredBuffer<Meshlet> _meshlets;
[[vk::binding(2, PER_DRAW)]] StructuredBuffer<InstanceData> _instanceDatas;

[[vk::binding(3, PER_DRAW)]] RWStructuredBuffer<uint> _culledIndices;
[[vk::binding(4, PER_DRAW)]] RWByteAddressBuffer _arguments;

struct CSInput
{
    int3 dispatchThreadID : SV_DispatchThreadID;
};

[numthreads(64, 1, 1)]
void main(CSInput input)
{
    uint baseMeshletOffset = input.dispatchThreadID.x * _constants.numMeshletsPerThread;
    if (baseMeshletOffset >= _constants.numMeshletsTotal)
        return;
    
    uint meshletOffsetRangeEnd = min(baseMeshletOffset + _constants.numMeshletsPerThread, _constants.numMeshletsTotal - 1);
    for (uint i = baseMeshletOffset; i < meshletOffsetRangeEnd; i++)
    {
        Meshlet meshlet = _meshlets[i];
        InstanceData instanceData = _instanceDatas[meshlet.instanceDataID];
        
        // Do Per Meshlet Culling Here
        {
        }
        
        uint offset;
        _arguments.InterlockedAdd(0, meshlet.indexCount, offset);
        
        for (uint j = 0; j < meshlet.indexCount; j++)
        {
            uint meshletGlobalIndex = instanceData.indexOffset + meshlet.indexStart + j;
            
            uint packedData = i | (uint(j) << 24);
            _culledIndices[offset + j] = packedData;
        }
    }
}