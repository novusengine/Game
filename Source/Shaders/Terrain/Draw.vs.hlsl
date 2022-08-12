permutation WIRE_FRAME = [0, 1];
#include "Terrain/Shared.inc.hlsl"
#include "globalData.inc.hlsl"

[[vk::binding(0, PER_DRAW)]] StructuredBuffer<float3> _vertexPositions;
[[vk::binding(1, PER_DRAW)]] StructuredBuffer<float3> _vertexNormals;
[[vk::binding(2, PER_DRAW)]] StructuredBuffer<uint2> _vertexMaterials;

[[vk::binding(3, PER_DRAW)]] StructuredBuffer<uint> _indices;
[[vk::binding(4, PER_DRAW)]] StructuredBuffer<ChunkData> _chunkDatas;
[[vk::binding(5, PER_DRAW)]] StructuredBuffer<MeshletData> _meshletDatas;
[[vk::binding(6, PER_DRAW)]] StructuredBuffer<SurvivedMeshletData> _survivedMeshletDatas;


struct VSInput
{
	uint packedData : SV_VertexID;
};

struct VSOutput
{
	float4 position : SV_POSITION;
	float3 worldPos : TEXCOORD0;
	float3 normal : TEXCOORD1;
	uint packedMaterials : TEXCOORD2;
	float4 blendValues : TEXCOORD3;
	uint meshletDataID : TEXCOORD4;
};

VSOutput main(VSInput input)
{
	uint meshletChunkID = input.packedData & 0x7FFFFF;
	uint localIndex = (input.packedData >> 23) & 0x1FF;

	SurvivedMeshletData survivedMeshletData = _survivedMeshletDatas[meshletChunkID];

	uint meshletDataID = survivedMeshletData.meshletDataID;
	uint chunkDataID = survivedMeshletData.chunkDataID;

	MeshletData meshletData = _meshletDatas[meshletDataID];
	ChunkData chunkData = _chunkDatas[chunkDataID];

	uint globalIndex = chunkData.indexOffset + meshletData.indexStart + localIndex;
	uint vertexID = chunkData.vertexOffset + _indices[globalIndex];

	VSOutput output;

	float3 vertexPosition = _vertexPositions[vertexID];
	float3 vertexNormal = _vertexNormals[vertexID];

	uint packedMaterials = _vertexMaterials[vertexID].x;
	uint packedBlendValues = _vertexMaterials[vertexID].y;

	float blendValueX = float((packedBlendValues >> 0) & 0xFF) / 255.0f;
	float blendValueY = float((packedBlendValues >> 8) & 0xFF) / 255.0f;
	float blendValueZ = float((packedBlendValues >> 16) & 0xFF) / 255.0f;
	float blendValueW = float((packedBlendValues >> 24) & 0xFF) / 255.0f;

	float4 blendValues = float4(blendValueX, blendValueY, blendValueZ, blendValueW);
	
#if WIRE_FRAME
	vertexPosition += vertexNormal * 0.01f;
#endif

	output.position = mul(float4(vertexPosition, 1.0f), _cameras[0].worldToClip);
	output.worldPos = vertexPosition;
	output.normal = normalize(vertexNormal);
	output.packedMaterials = packedMaterials;
	output.blendValues = blendValues;
	output.meshletDataID = meshletDataID;

	return output;
}