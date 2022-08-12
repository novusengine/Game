#pragma once
#include "Game/Rendering/Types.h"
#include "Game/Rendering/Terrain/ChunkData.h"

#include <Base/Types.h>
#include <Renderer/GPUVector.h>

#include <array>

namespace TerrainUtils
{
	constexpr vec3 TERRAIN_VOXEL_GENERATION_SCALE = vec3(0.25f, 0.25f, 0.25f);

	constexpr f32 TERRAIN_VOXEL_SIZE_X = 1.0f;
	constexpr f32 TERRAIN_VOXEL_SIZE_Y = 1.0f;
	constexpr f32 TERRAIN_VOXEL_SIZE_Z = 1.0f;
	constexpr vec3 TERRAIN_VOXEL_SIZE = vec3(TERRAIN_VOXEL_SIZE_X, TERRAIN_VOXEL_SIZE_Y, TERRAIN_VOXEL_SIZE_Z);

	constexpr u32 TERRAIN_VOXEL_BORDER = 1;

	constexpr u32 TERRAIN_CHUNK_NUM_VOXELS_X = 128 + TERRAIN_VOXEL_BORDER;
	constexpr u32 TERRAIN_CHUNK_NUM_VOXELS_Y = 128 + TERRAIN_VOXEL_BORDER;
	constexpr u32 TERRAIN_CHUNK_NUM_VOXELS_Z = 128 + TERRAIN_VOXEL_BORDER;
	constexpr ivec3 TERRAIN_CHUNK_NUM_VOXELS = ivec3(TERRAIN_CHUNK_NUM_VOXELS_X, TERRAIN_CHUNK_NUM_VOXELS_Y, TERRAIN_CHUNK_NUM_VOXELS_Z);
	constexpr u32 TERRAIN_CHUNK_NUM_TOTAL_VOXELS = TERRAIN_CHUNK_NUM_VOXELS_X * TERRAIN_CHUNK_NUM_VOXELS_Y * TERRAIN_CHUNK_NUM_VOXELS_Z;

	constexpr f32 TERRAIN_CHUNK_SIZE_X = TERRAIN_CHUNK_NUM_VOXELS_X * TERRAIN_VOXEL_SIZE_X;
	constexpr f32 TERRAIN_CHUNK_SIZE_Y = TERRAIN_CHUNK_NUM_VOXELS_Y * TERRAIN_VOXEL_SIZE_Y;
	constexpr f32 TERRAIN_CHUNK_SIZE_Z = TERRAIN_CHUNK_NUM_VOXELS_Z * TERRAIN_VOXEL_SIZE_Z;
	constexpr vec3 TERRAIN_CHUNK_SIZE = vec3(TERRAIN_CHUNK_SIZE_X, TERRAIN_CHUNK_SIZE_Y, TERRAIN_CHUNK_SIZE_Z);

	constexpr u32 TERRAIN_WORLD_NUM_CHUNKS_X = 1;
	constexpr u32 TERRAIN_WORLD_NUM_CHUNKS_Y = 1;
	constexpr u32 TERRAIN_WORLD_NUM_CHUNKS_Z = 1;
	constexpr u32 TERRAIN_WORLD_NUM_TOTAL_VOXELS = TERRAIN_WORLD_NUM_CHUNKS_X * TERRAIN_WORLD_NUM_CHUNKS_Y * TERRAIN_WORLD_NUM_CHUNKS_Z * TERRAIN_CHUNK_NUM_TOTAL_VOXELS;

	constexpr f32 TERRAIN_WORLD_SIZE_X = TERRAIN_WORLD_NUM_CHUNKS_X * TERRAIN_CHUNK_SIZE_X;
	constexpr f32 TERRAIN_WORLD_SIZE_Y = TERRAIN_WORLD_NUM_CHUNKS_Y * TERRAIN_CHUNK_SIZE_Y;
	constexpr f32 TERRAIN_WORLD_SIZE_Z = TERRAIN_WORLD_NUM_CHUNKS_Z * TERRAIN_CHUNK_SIZE_Z;

	inline i32 VoxelCoordToIndex(i32 x, i32 y, i32 z)
	{
		return (z * TERRAIN_CHUNK_NUM_VOXELS_X * TERRAIN_CHUNK_NUM_VOXELS_Y) + (y * TERRAIN_CHUNK_NUM_VOXELS_X) + x;
	}

	inline i32 VoxelCoordToIndex(f32 x, f32 y, f32 z)
	{
		return VoxelCoordToIndex(static_cast<i32>(x), static_cast<i32>(y), static_cast<i32>(z));
	}

	inline ivec3 VoxelIndexToCoord(i32 idx)
	{
		ivec3 coord;
		coord.z = idx / (TERRAIN_CHUNK_NUM_VOXELS_X * TERRAIN_CHUNK_NUM_VOXELS_Y);
		idx -= (coord.z * TERRAIN_CHUNK_NUM_VOXELS_X * TERRAIN_CHUNK_NUM_VOXELS_Y);
		coord.y = idx / TERRAIN_CHUNK_NUM_VOXELS_X;
		coord.x = idx % TERRAIN_CHUNK_NUM_VOXELS_X;
		return coord;
	}
}

class Chunk
{
public:
	static const u32 WIDTH = TerrainUtils::TERRAIN_CHUNK_NUM_VOXELS_X;
	static const u32 HEIGHT = TerrainUtils::TERRAIN_CHUNK_NUM_VOXELS_Y;
	static const u32 DEPTH = TerrainUtils::TERRAIN_CHUNK_NUM_VOXELS_Z;
	static const u32 BORDER = TerrainUtils::TERRAIN_VOXEL_BORDER;

	struct Vertex
	{
		vec4 position;
		vec4 normal;
	};

public:
	Chunk(const ivec3& chunk3DIndex);

	std::array<f32, TerrainUtils::TERRAIN_CHUNK_NUM_TOTAL_VOXELS>& GetVoxels() { return _voxels; }
	std::array<u8, TerrainUtils::TERRAIN_CHUNK_NUM_TOTAL_VOXELS>& GetVoxelMaterials() { return _voxelMaterials; }

	void Meshify(f32 target, SafeVector<vec4>& safeVertexPositions, SafeVector<vec4>& safeVertexNormals, SafeVector<u32>& indices, SafeVector<Meshlet>& safeMeshlets, SafeVector<uvec2>& safeVertexMaterials, SafeVector<ChunkData>& safeChunkDatas, size_t numMaterials);
	const ivec3& GetChunk3DIndex() { return _chunk3DIndex; }

private:
	void MarchingCubes(i32 x, i32 y, i32 z, f32 target, std::vector<vec4>& vertexPositions, std::vector<uvec2>& vertexMaterials, size_t numMaterials);

	void FillCube(i32 x, i32 y, i32 z, f32* cube, u8* materials);
	f32 GetOffset(f32 v1, f32 v2, f32 target);

	vec3 CreateVertex(vec3 position, vec3 center, vec3 size);
private:
	ivec3 _chunk3DIndex;
	std::array<f32, TerrainUtils::TERRAIN_CHUNK_NUM_TOTAL_VOXELS> _voxels{};
	std::array<u8, TerrainUtils::TERRAIN_CHUNK_NUM_TOTAL_VOXELS> _voxelMaterials{};
};