#include "globalData.inc.hlsl"

struct Vertex
{
	float4 position;
	float4 normal;
};

[[vk::binding(0, PER_DRAW)]] StructuredBuffer<Vertex> _vertices;

struct VSInput
{
	uint vertexID : SV_VertexID;
};

struct VSOutput
{
	float4 position : SV_POSITION;
	float3 normal : TEXCOORD0;
};

VSOutput main(VSInput input)
{
	VSOutput output;

	Vertex vertex = _vertices[input.vertexID];

	output.position = mul(vertex.position, _cameras[0].worldToClip);
	output.normal = vertex.normal.xyz;

	return output;
}