
#include "DescriptorSet/Global.inc.slang"

struct Vertex3D
{
	float4 pos;
	float4 normalAndColor; // xyz = normal, int(w) = color
};

[[vk::binding(0, PER_PASS)]] StructuredBuffer<Vertex3D> _vertices;

struct VSInput
{
	uint vertexID : SV_VulkanVertexID;
};

struct VSOutput
{
	float4 pos : SV_Position;
	float3 normal : TEXCOORD0;
	float4 color : Color;
};

float4 GetVertexColor(uint inColor)
{
	float4 color;

	color.r = ((inColor & 0xff000000) >> 24) / 255.0f;
	color.g = ((inColor & 0x00ff0000) >> 16) / 255.0f;
	color.b = ((inColor & 0x0000ff00) >> 8) / 255.0f;
	color.a = (inColor & 0x000000ff) / 255.0f;

	return color;
}

[shader("vertex")]
VSOutput main(VSInput input)
{
	Vertex3D vertex = _vertices[input.vertexID];

	uint color = asuint(vertex.normalAndColor.w);
	float3 normal = vertex.normalAndColor.xyz;

	VSOutput output;
	output.pos = mul(float4(vertex.pos.xyz, 1.0f), _cameras[0].worldToClip);
	output.normal = normal;
	output.color = GetVertexColor(color);
	return output;
}
