#ifndef TERRAIN_SET_INCLUDED
#define TERRAIN_SET_INCLUDED

#include "Include/Common.inc.slang"

#define NUM_CHUNKS_PER_MAP_SIDE (64)

// WARNING: For now, when you change this file, make sure you recompile ALL shaders or you might get descriptor set reflection issues
// This will be fixed later when we have better dependency tracking

[[vk::binding(0, TERRAIN)]] StructuredBuffer<PackedCellData> _packedCellData;
[[vk::binding(1, TERRAIN)]] StructuredBuffer<PackedTerrainVertex> _packedTerrainVertices;
[[vk::binding(2, TERRAIN)]] StructuredBuffer<InstanceData> _instanceDatas;
[[vk::binding(3, TERRAIN)]] StructuredBuffer<InstanceData> _culledInstanceDatas;
[[vk::binding(4, TERRAIN)]] StructuredBuffer<ChunkData> _chunkData;
[[vk::binding(5, TERRAIN)]] SamplerState _alphaSampler;
[[vk::binding(6, TERRAIN)]] Texture2DArray<float4> _terrainAlphaTextures[NUM_CHUNKS_PER_MAP_SIDE * NUM_CHUNKS_PER_MAP_SIDE];
[[vk::binding(7, TERRAIN)]] Texture2D<float4> _terrainColorTextures[]; // IMPORTANT: This has to be the last element in this set!

#endif // TERRAIN_SET_INCLUDED