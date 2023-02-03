#pragma once
#include <Base/Types.h>
#include <Base/Math/Geometry.h>

#include <FileFormat/Warcraft/Shared.h>

#include <Renderer/DescriptorSet.h>
#include <Renderer/FrameResource.h>
#include <Renderer/GPUVector.h>

class DebugRenderer;
struct RenderResources;

namespace Renderer
{
	class Renderer;
	class RenderGraph;
	class RenderGraphResources;
}

namespace Map
{
	struct Chunk;
}

class TerrainRenderer
{
public:
	TerrainRenderer(Renderer::Renderer* renderer, DebugRenderer* debugRenderer);
	~TerrainRenderer();

	void Update(f32 deltaTime);
	void Clear();

	void AddOccluderPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);
	void AddCullingPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);
	void AddGeometryPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);

	void ReserveChunks(u32 numChunks);
	u32 AddChunk(u32 chunkHash, Map::Chunk* chunk, ivec2 chunkGridPos);

	Renderer::DescriptorSet& GetMaterialPassDescriptorSet() { return _materialPassDescriptorSet; }

	// Drawcall stats
	u32 GetNumDrawCalls() { return Terrain::CHUNK_NUM_CELLS * _numChunksLoaded; }
	u32 GetNumOccluderDrawCalls() { return _numOccluderDrawCalls; }
	u32 GetNumSurvivingDrawCalls(u32 viewID) { return _numSurvivingDrawCalls[viewID]; }

	// Triangle stats
	u32 GetNumTriangles() { return Terrain::CHUNK_NUM_CELLS * _numChunksLoaded * Terrain::CELL_NUM_TRIANGLES; }
	u32 GetNumOccluderTriangles() { return _numOccluderDrawCalls * Terrain::CELL_NUM_TRIANGLES; }
	u32 GetNumSurvivingGeometryTriangles(u32 viewID) { return _numSurvivingDrawCalls[viewID] * Terrain::CELL_NUM_TRIANGLES; }

private:
	void CreatePermanentResources();

	void SyncToGPU();

	struct DrawParams
	{
		bool shadowPass = false;
		u32 shadowCascade = 0;
		bool cullingEnabled = false;
		Renderer::RenderPassMutableResource visibilityBuffer;
		Renderer::RenderPassMutableResource depth;
		Renderer::BufferID instanceBuffer;
		Renderer::BufferID argumentBuffer;
		u32 argumentsIndex = 0;
	};
	void Draw(const RenderResources& resources, u8 frameIndex, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList, const DrawParams& params);

private:
	PRAGMA_NO_PADDING_START
	struct TerrainVertex
	{
		u8 normal[3] = { 0, 0, 0 };
		u8 color[3] = { 0, 0, 0 };
		f16 height = f16(0);
	};

	struct InstanceData
	{
		u32 packedChunkCellID = 0;
		u32 globalCellID = 0;
	};

	struct CellData
	{
		u16 diffuseIDs[4] = { 0, 0, 0, 0 };
		u64 hole = 0;
	};

	struct ChunkData
	{
		u32 alphaMapID = 0;
	};

	struct CellHeightRange
	{
		f16 min = f16(0);
		f16 max = f16(0);
	};
	PRAGMA_NO_PADDING_END

private:
	Renderer::Renderer* _renderer = nullptr;
	DebugRenderer* _debugRenderer = nullptr;

	Renderer::GPUVector<u16> _cellIndices;
	Renderer::GPUVector<TerrainVertex> _vertices;
	Renderer::GPUVector<InstanceData> _instanceDatas;

	Renderer::GPUVector<CellData> _cellDatas;
	Renderer::GPUVector<ChunkData> _chunkDatas;

	Renderer::GPUVector<CellHeightRange> _cellHeightRanges;

	// GPU-only workbuffers
	Renderer::BufferID _occluderArgumentBuffer;
	Renderer::BufferID _argumentBuffer;

	Renderer::BufferID _occluderDrawCountReadBackBuffer;
	Renderer::BufferID _drawCountReadBackBuffer;
	
	FrameResource<Renderer::BufferID, 2> _culledInstanceBitMaskBuffer;
	Renderer::BufferID _culledInstanceBuffer[Renderer::Settings::MAX_VIEWS];

	Renderer::TextureArrayID _textures;
	Renderer::TextureArrayID _alphaTextures;

	Renderer::SamplerID _colorSampler;
	Renderer::SamplerID _alphaSampler;
	Renderer::SamplerID _occlusionSampler;

	Renderer::DescriptorSet _occluderFillPassDescriptorSet;
	Renderer::DescriptorSet _cullingPassDescriptorSet;
	Renderer::DescriptorSet _geometryPassDescriptorSet;
	Renderer::DescriptorSet _materialPassDescriptorSet;

	std::vector<Geometry::AABoundingBox> _cellBoundingBoxes;
	std::vector<Geometry::AABoundingBox> _chunkBoundingBoxes;

	std::atomic<u32> _numChunksLoaded = 0;

	u32 _numOccluderDrawCalls = 0;
	u32 _numSurvivingDrawCalls[Renderer::Settings::MAX_VIEWS] = { 0 };
};