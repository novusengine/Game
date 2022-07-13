#include "globalData.inc.hlsl"

struct Vertex
{
    float3 position;
    float3 normal;
    float2 uv;
};

[[vk::binding(0, PER_DRAW)]] StructuredBuffer<Vertex> _vertices;

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
    Vertex vertex = _vertices[input.vertexID];
    
    VSOutput output;
    output.position = mul(float4(vertex.position, 1.0f), _cameras[0].worldToClip);
    output.normal = vertex.normal;
    output.uv = vertex.uv;
    
    return output;
}