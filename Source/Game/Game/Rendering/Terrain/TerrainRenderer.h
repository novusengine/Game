#pragma once
#include "TerrainUtils.h"
#include "Game/Rendering/Types.h"
#include "Game/Rendering/Terrain/ChunkData.h"

#include <Base/Types.h>
#include <Renderer/DescriptorSet.h>

class DebugRenderer;
struct RenderResources;
class SimplexNoise;

namespace Renderer
{
	class Renderer;
	class RenderGraph;
}

class TerrainRenderer
{
public:
	struct TerrainMaterial
	{
	public:
		u32 xTextureID = 0;
		u32 yTextureID = 0;
		u32 zTextureID = 0;
		u32 padding = 0;
	};

	struct SurvivedChunkData
	{
	public:
		u32 chunkDataID;
		u32 numMeshletsBeforeChunk;
	};

	struct SurvivedMeshletData
	{
	public:
		u32 meshletDataID = 0;
		u32 chunkDataID = 0;
	};

public:
	TerrainRenderer(Renderer::Renderer* renderer, DebugRenderer* debugRenderer);
	~TerrainRenderer();

	void Update(f32 deltaTime);
	void Clear();

	void AddCullingPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);
	void AddGeometryPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);
	
private:
	void CreatePermanentResources();

	void SyncToGPU();

	void GenerateVoxels(Chunk& chunk);
	f32 GenerateVoxel(ivec3 chunkCoord, ivec3 voxelCoord, u8& materialID);
	f32 GetSurfaceLevel(vec3 noisePos);

private:
	Renderer::Renderer* _renderer = nullptr;
	DebugRenderer* _debugRenderer = nullptr;

	bool _showWireFrame = false;
	bool _showVoxelPoints = false;
	f32 _maxVoxelValue = 1.0f;
	u32 _debugMode = 0;
	f32 _sharpness = 6.0f;

	// Generation settings
	i32 _octaves = 4;
	f32 _frequency = 0.01f;
	f32 _amplitude = 10.0f;
	f32 _heightSampleDistance = 1.0f;
	i32 _heightSteps = 1;

	f32 _stoneThreshold = 2.0f;
	f32 _stoneDirtMixThreshold = 1.5f;
	f32 _dirtThreshold = 1.2f;
	f32 _dirtGrassMixThreshold = 0.8f;

	Chunk _chunk;
	//Chunk _chunk2;

	Renderer::SamplerID _linearSampler;
	Renderer::TextureArrayID _terrainTextures;

	Renderer::BufferID _indirectDrawArgumentBuffer;
	Renderer::BufferID _indirectDispatchArgumentBuffer;
	Renderer::BufferID _survivedMeshletBuffer;
	Renderer::BufferID _survivedMeshletCountBuffer;
	Renderer::BufferID _survivedChunkDataBuffer;

	Renderer::BufferID _culledIndexBuffer;

	Renderer::GPUVector<vec4> _vertexPositions;
	Renderer::GPUVector<vec4> _vertexNormals;
	Renderer::GPUVector<uvec2> _vertexMaterials;
	Renderer::GPUVector<u32> _indices;

	Renderer::GPUVector<ChunkData> _chunkDatas;
	Renderer::GPUVector<Meshlet> _meshletDatas;
	Renderer::GPUVector<TerrainMaterial> _materials;

	Renderer::DescriptorSet _cullDescriptorSet;
	Renderer::DescriptorSet _geometryDescriptorSet;
};