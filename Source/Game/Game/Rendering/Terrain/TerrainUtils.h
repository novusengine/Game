#pragma once
#include <Base/Types.h>
#include <Renderer/GPUVector.h>

#include <array>

namespace TerrainUtils
{
	constexpr f32 TERRAIN_VOXEL_SIZE_X = 1.0f;
	constexpr f32 TERRAIN_VOXEL_SIZE_Y = 1.0f;
	constexpr f32 TERRAIN_VOXEL_SIZE_Z = 1.0f;

	constexpr u32 TERRAIN_VOXEL_BORDER = 1;

	constexpr u32 TERRAIN_CHUNK_NUM_VOXELS_X = 32 + TERRAIN_VOXEL_BORDER;
	constexpr u32 TERRAIN_CHUNK_NUM_VOXELS_Y = 32 + TERRAIN_VOXEL_BORDER;
	constexpr u32 TERRAIN_CHUNK_NUM_VOXELS_Z = 32 + TERRAIN_VOXEL_BORDER;
	constexpr u32 TERRAIN_CHUNK_NUM_VOXELS = TERRAIN_CHUNK_NUM_VOXELS_X * TERRAIN_CHUNK_NUM_VOXELS_Y * TERRAIN_CHUNK_NUM_VOXELS_Z;

	constexpr f32 TERRAIN_CHUNK_SIZE_X = TERRAIN_CHUNK_NUM_VOXELS_X * TERRAIN_VOXEL_SIZE_X;
	constexpr f32 TERRAIN_CHUNK_SIZE_Y = TERRAIN_CHUNK_NUM_VOXELS_Y * TERRAIN_VOXEL_SIZE_Y;
	constexpr f32 TERRAIN_CHUNK_SIZE_Z = TERRAIN_CHUNK_NUM_VOXELS_Z * TERRAIN_VOXEL_SIZE_Z;

	constexpr u32 TERRAIN_WORLD_NUM_CHUNKS_X = 1;
	constexpr u32 TERRAIN_WORLD_NUM_CHUNKS_Y = 1;
	constexpr u32 TERRAIN_WORLD_NUM_CHUNKS_Z = 1;
	constexpr u32 TERRAIN_WORLD_NUM_VOXELS = TERRAIN_WORLD_NUM_CHUNKS_X * TERRAIN_WORLD_NUM_CHUNKS_Y * TERRAIN_WORLD_NUM_CHUNKS_Z * TERRAIN_CHUNK_NUM_VOXELS;

	constexpr f32 TERRAIN_WORLD_SIZE_X = TERRAIN_WORLD_NUM_CHUNKS_X * TERRAIN_CHUNK_SIZE_X;
	constexpr f32 TERRAIN_WORLD_SIZE_Y = TERRAIN_WORLD_NUM_CHUNKS_Y * TERRAIN_CHUNK_SIZE_Y;
	constexpr f32 TERRAIN_WORLD_SIZE_Z = TERRAIN_WORLD_NUM_CHUNKS_Z * TERRAIN_CHUNK_SIZE_Z;
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
	std::array<f32, TerrainUtils::TERRAIN_CHUNK_NUM_VOXELS>& GetVoxels() { return _voxels; }

	void Meshify(f32 target);
private:
	void MarchingCubes(i32 x, i32 y, i32 z, f32 target);

	vec3 GetNormal(i32 x, i32 y, i32 z);
	void FillCube(i32 x, i32 y, i32 z, f32* cube);
	f32 GetOffset(f32 v1, f32 v2, f32 target);

	Vertex CreateVertex(vec3 position, vec3 center, vec3 size, vec3 normal);

private:
	
	std::array<f32, TerrainUtils::TERRAIN_CHUNK_NUM_VOXELS> _voxels{};
	Renderer::GPUVector<Vertex> _vertices;
};