#include "Model/Shared.inc.hlsl"

struct Constants
{
    uint numInstances;
};
[[vk::push_constant]] Constants _constants;

[[vk::binding(0, PER_DRAW)]] StructuredBuffer<ModelData> _modelDatas;
[[vk::binding(1, PER_DRAW)]] StructuredBuffer<InstanceData> _instanceDatas;
[[vk::binding(2, PER_DRAW)]] StructuredBuffer<uint64_t> _dispatchArguments;

[[vk::binding(3, PER_DRAW)]] RWStructuredBuffer<SurvivedInstanceData> _survivedInstanceDatas;

struct CSInput
{
    int3 dispatchThreadID : SV_DispatchThreadID;
};

[numthreads(64, 1, 1)]
void main(CSInput input)
{
    uint instanceDataID = input.dispatchThreadID.x;
    if (instanceDataID >= _constants.numInstances)
        return;
    
    InstanceData instanceData = _instanceDatas[instanceDataID];
    ModelData modelData = _modelDatas[instanceData.modelDataID];
    
    // Do Instance Culling Here
    {
        
    }
    
    uint64_t meshletCount = modelData.packedData & 0xFFFF;
    uint64_t value = 1 | (meshletCount << 32);
    uint64_t originalValue;
    InterlockedAdd(_dispatchArguments[0], value, originalValue);
    
    uint survivedInstanceDataID = uint(originalValue);
    
    SurvivedInstanceData survivedInstanceData;
    survivedInstanceData.instanceDataID = instanceDataID;
    survivedInstanceData.numMeshletsBeforeInstance = uint(originalValue >> 32);
    
    _survivedInstanceDatas[survivedInstanceDataID] = survivedInstanceData;
}