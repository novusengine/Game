#include "globalData.inc.hlsl"
#include "Model/Shared.inc.hlsl"

[[vk::binding(0, PER_DRAW)]] StructuredBuffer<float4> _vertexPositions;
[[vk::binding(1, PER_DRAW)]] StructuredBuffer<float4> _vertexNormals;
[[vk::binding(2, PER_DRAW)]] StructuredBuffer<float2> _vertexUVs;

[[vk::binding(3, PER_DRAW)]] StructuredBuffer<uint> _indices;
[[vk::binding(4, PER_DRAW)]] StructuredBuffer<Meshlet> _meshlets;
[[vk::binding(5, PER_DRAW)]] StructuredBuffer<InstanceData> _instanceDatas;
[[vk::binding(6, PER_DRAW)]] StructuredBuffer<float4x4> _instanceMatrices;

struct VSInput
{
    uint packedData : SV_VertexID;
};

struct VSOutput
{
    float4 position : SV_Position;
    float3 normal : TEXCOORD0;
    float2 uv : TEXCOORD1;
    uint meshletID : TEXCOORD2;
};

VSOutput main(VSInput input)
{
    uint meshletID = input.packedData & 0xFFFFFF;
    uint localIndex = input.packedData >> 24 & 0xFF;
    
    Meshlet meshlet = _meshlets[meshletID];
    InstanceData instanceData = _instanceDatas[meshlet.instanceDataID];
    float4x4 instanceMatrix = _instanceMatrices[meshlet.instanceDataID];
    
    uint globalIndex = instanceData.indexOffset + meshlet.indexStart + localIndex;
    uint vertexID = _indices[globalIndex];
    
    VSOutput output;
    
    float4 position = mul(float4(_vertexPositions[vertexID].xyz, 1.0f), instanceMatrix);
    output.position = mul(position, _cameras[0].worldToClip);
    output.normal = _vertexNormals[vertexID].xyz;
    output.uv = _vertexUVs[vertexID];
    output.meshletID = meshletID;
    
    return output;
}