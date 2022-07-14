#include "globalData.inc.hlsl"

[[vk::binding(0, PER_DRAW)]] StructuredBuffer<float4> _vertexPositions;
[[vk::binding(1, PER_DRAW)]] StructuredBuffer<float4> _vertexNormals;
[[vk::binding(2, PER_DRAW)]] StructuredBuffer<float2> _vertexUVs;

struct VSInput
{
    uint vertexID : SV_VertexID;
};

struct VSOutput
{
    float4 position : SV_Position;
    float3 normal : TEXCOORD0;
    float2 uv : TEXCOORD1;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    output.position = mul(float4(_vertexPositions[input.vertexID].xyz, 1.0f), _cameras[0].worldToClip);
    output.normal = _vertexNormals[input.vertexID].xyz;
    output.uv = _vertexUVs[input.vertexID];
    
    return output;
}