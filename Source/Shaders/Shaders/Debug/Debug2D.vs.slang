
#include "DescriptorSet/Debug.inc.slang"

struct Vertex2D
{
	float2 pos;
	uint color;
	uint padding;
};

[[vk::binding(0, PER_PASS)]] StructuredBuffer<Vertex2D> _vertices;

struct VSInput
{
	uint vertexID : SV_VulkanVertexID;
};

struct VSOutput
{
	float4 pos : SV_Position;
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
	Vertex2D vertex = _vertices[input.vertexID];

	VSOutput output;
	output.pos = float4((vertex.pos.x * 2.0f) - 1.f, (vertex.pos.y * 2.0f) - 1.f, 0.f, 1.0f);
	output.color = GetVertexColor(vertex.color);
	return output;
}
