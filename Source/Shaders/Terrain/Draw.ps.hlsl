permutation SUPPORTS_EXTENDED_TEXTURES = [0, 1];
#define GEOMETRY_PASS 1

#include "common.inc.hlsl"
#include "globalData.inc.hlsl"
#include "Terrain/Shared.inc.hlsl"
#include "Include/VisibilityBuffers.inc.hlsl"

struct PSInput
{
	uint triangleID : SV_PrimitiveID;
	uint instanceID : TEXCOORD0;
	float3 worldPosition : TEXCOORD1;
};

struct PSOutput
{
	uint4 visibilityBuffer : SV_Target0;
};

PSOutput main(PSInput input)
{
	PSOutput output;

	const uint padding = 0;

	// Get the vertexIDs of the triangle we're in
	InstanceData instanceData = _instanceDatas[input.instanceID];

	// Get the cellID and chunkID
	const uint cellID = instanceData.packedChunkCellID & 0xFFFF;
	const uint chunkID = instanceData.packedChunkCellID >> 16;

	uint3 localVertexIDs = GetLocalTerrainVertexIDs(input.triangleID);

	uint globalVertexOffset = instanceData.globalCellID * NUM_VERTICES_PER_CELL;

	// Load the vertices
	TerrainVertex vertices[3];

	[unroll]
	for (uint i = 0; i < 3; i++)
	{
		vertices[i] = LoadTerrainVertex(chunkID, cellID, globalVertexOffset, localVertexIDs[i]);
	}

	// Calculate Barycentrics
	float2 barycentrics = NBLCalculateBarycentrics(input.worldPosition, float3x3(vertices[0].position.xyz, vertices[1].position.xyz, vertices[2].position.xyz));

	float2 ddxBarycentrics = ddx(barycentrics);
	float2 ddyBarycentrics = ddy(barycentrics);

	output.visibilityBuffer = PackVisibilityBuffer(ObjectType::Terrain, input.instanceID, input.triangleID, barycentrics, ddxBarycentrics, ddyBarycentrics);
	return output;
}