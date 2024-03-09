#include "Model/Shared.inc.hlsl"

/*struct PaddedDispatch
{
    uint numSurvivedInstances;
    uint x;
    uint y;
    uint z;
};

[[vk::binding(0, PER_DRAW)]] StructuredBuffer<uint> _indices;
[[vk::binding(1, PER_DRAW)]] StructuredBuffer<ModelData> _modelDatas;
[[vk::binding(2, PER_DRAW)]] StructuredBuffer<InstanceData> _instanceDatas;
[[vk::binding(3, PER_DRAW)]] StructuredBuffer<MeshletData> _meshletDatas;
[[vk::binding(4, PER_DRAW)]] StructuredBuffer<PaddedDispatch> _dispatchArguments;

[[vk::binding(5, PER_DRAW)]] RWStructuredBuffer<uint> _culledIndices;
[[vk::binding(6, PER_DRAW)]] RWStructuredBuffer<SurvivedInstanceData> _survivedInstanceDatas;
[[vk::binding(7, PER_DRAW)]] RWStructuredBuffer<MeshletInstance> _meshletInstances;
[[vk::binding(8, PER_DRAW)]] RWByteAddressBuffer _meshletInstanceCount;
[[vk::binding(9, PER_DRAW)]] RWByteAddressBuffer _drawArguments;*/

struct CSInput
{
    int3 dispatchThreadID : SV_DispatchThreadID;
};

[numthreads(64, 1, 1)]
void main(CSInput input)
{
    /*uint survivedInstanceID = input.dispatchThreadID.x;
    uint numSurvivedInstances = _dispatchArguments[0].numSurvivedInstances;
    
    if (survivedInstanceID >= numSurvivedInstances)
        return;

    SurvivedInstanceData survivedInstanceData = _survivedInstanceDatas[survivedInstanceID];
    InstanceData instanceData = _instanceDatas[survivedInstanceData.instanceDataID];
    ModelData modelData = _modelDatas[instanceData.modelDataID];
    
    uint meshletCount = modelData.packedData & 0xFFFF;
    
    uint offset;
    uint meshletBaseInstanceID;
    _meshletInstanceCount.InterlockedAdd(0, meshletCount, meshletBaseInstanceID);
    
    for (int i = 0; i < meshletCount; i++)
    {
        uint meshletIndex = modelData.meshletOffset + i;
        MeshletData meshletData = _meshletDatas[meshletIndex];
        
        // Do Per Meshlet Culling Here
        {
        
        }
    
        uint meshletInstanceID = meshletBaseInstanceID + i;
    
        MeshletInstance meshletInstance;
        meshletInstance.meshletDataID = meshletIndex;
        meshletInstance.instanceDataID = survivedInstanceData.instanceDataID;
        _meshletInstances[meshletInstanceID] = meshletInstance;
    
        _drawArguments.InterlockedAdd(0, meshletData.indexCount, offset);

        for (uint j = 0; j < meshletData.indexCount; j++)
        {
            uint packedData = meshletInstanceID | (j << 23);
            _culledIndices[offset + j] = packedData;
        }
    }*/
}