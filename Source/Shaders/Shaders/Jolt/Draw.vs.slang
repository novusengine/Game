
#include "DescriptorSet/Global.inc.slang"

struct PackedVertex
{
	float4 posAndUVx; // xyz = pos, w = uv.x
	float4 normalAndUVy; // xyz = normal, w = uv.y
	float4 color;
};

struct Vertex
{
	float3 pos;
	float3 normal;
	float2 uv;
	float4 color;
};

struct InstanceRef
{
	uint instanceID;
	uint drawID;
	uint extraID; // Unused for Jolt
	uint padding;
};

struct DrawCallData
{
    uint baseInstanceLookupOffset;
    uint padding;
};

[[vk::binding(0, PER_PASS)]] StructuredBuffer<PackedVertex> _vertices;
[[vk::binding(1, PER_PASS)]] StructuredBuffer<uint> _culledInstanceLookupTable; // One uint per instance, contains instanceRefID of what survives culling, and thus can get reordered
[[vk::binding(2, PER_PASS)]] StructuredBuffer<InstanceRef> _instanceRefTable;
[[vk::binding(3, PER_PASS)]] StructuredBuffer<float4x4> _instances;

struct VSInput
{
	uint vertexID : SV_VulkanVertexID;
	uint culledInstanceID : SV_VulkanInstanceID;
};

struct VSOutput
{
	float4 pos : SV_Position;
	float3 normal : TEXCOORD0;
	float4 color : Color;
};

Vertex LoadVertex(uint vertexID)
{
	PackedVertex packedVertex = _vertices[vertexID];

	Vertex vertex;
	vertex.pos = packedVertex.posAndUVx.xyz;
	vertex.normal = packedVertex.normalAndUVy.xyz;
	vertex.uv.x = packedVertex.posAndUVx.w;
	vertex.uv.y = packedVertex.normalAndUVy.w;
	vertex.color = packedVertex.color;
	return vertex;
}

[shader("vertex")]
VSOutput main(VSInput input)
{
	uint instanceRefID = _culledInstanceLookupTable[input.culledInstanceID];
	InstanceRef instanceRef = _instanceRefTable[instanceRefID];

	Vertex vertex = LoadVertex(input.vertexID);
	float4x4 instance = _instances[instanceRef.instanceID];

	float3 pos = mul(float4(vertex.pos, 1.0f), instance).xyz;
	float3 normal = mul(float4(vertex.normal, 0.0f), instance).xyz;

	VSOutput output;
	output.pos = mul(float4(pos, 1.0f), _cameras[0].worldToClip);
	output.normal = vertex.normal;
	output.color = vertex.color;
	return output;
}
