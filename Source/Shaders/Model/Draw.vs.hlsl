#include "globalData.inc.hlsl"
#include "Model/Shared.inc.hlsl"

[[vk::binding(0, PER_DRAW)]] StructuredBuffer<float4> _vertexPositions;
[[vk::binding(1, PER_DRAW)]] StructuredBuffer<float4> _vertexNormals;
[[vk::binding(2, PER_DRAW)]] StructuredBuffer<float2> _vertexUVs;

[[vk::binding(3, PER_DRAW)]] StructuredBuffer<uint> _indices;
[[vk::binding(4, PER_DRAW)]] StructuredBuffer<ModelData> _modelDatas;
[[vk::binding(5, PER_DRAW)]] StructuredBuffer<InstanceData> _instanceDatas;
[[vk::binding(6, PER_DRAW)]] StructuredBuffer<MeshletData> _meshletDatas;
[[vk::binding(7, PER_DRAW)]] StructuredBuffer<MeshletInstance> _meshletInstances;
[[vk::binding(8, PER_DRAW)]] StructuredBuffer<float4x4> _instanceMatrices;

struct VSInput
{
    uint packedData : SV_VertexID;
};

struct VSOutput
{
    float4 position : SV_Position;
    float3 normal : TEXCOORD0;
    float2 uv : TEXCOORD1;
    uint meshletDataID : TEXCOORD2;
};

VSOutput main(VSInput input)
{
    uint meshletInstanceID = input.packedData & 0x7FFFFF;
    uint localIndex = (input.packedData >> 23) & 0x1FF;
    
    MeshletInstance meshletInstance = _meshletInstances[meshletInstanceID];
    
    uint meshletDataID = meshletInstance.meshletDataID;
    uint instanceDataID = meshletInstance.instanceDataID;
    
    MeshletData meshletData = _meshletDatas[meshletDataID];
    InstanceData instanceData = _instanceDatas[instanceDataID];
    float4x4 instanceMatrix = _instanceMatrices[instanceDataID];
    ModelData modelData = _modelDatas[instanceData.modelDataID];
    
    uint globalIndex = modelData.indexOffset + meshletData.indexStart + localIndex;
    uint vertexID = modelData.vertexOffset + _indices[globalIndex];
    
    VSOutput output;
    
    float4 position = mul(float4(_vertexPositions[vertexID].xyz, 1.0f), instanceMatrix);
    output.position = mul(position, _cameras[0].worldToClip);
    output.normal = _vertexNormals[vertexID].xyz;
    output.uv = _vertexUVs[vertexID];
    output.meshletDataID = instanceData.modelDataID;
    
    return output;
}