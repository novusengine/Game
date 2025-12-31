#pragma once
#include <Base/Types.h>
#include <Base/Math/Geometry.h>

#include <FileFormat/Shared.h>

#include <Renderer/DescriptorSet.h>
#include <Renderer/DescriptorSetResource.h>
#include <Renderer/FrameResource.h>
#include <Renderer/GPUVector.h>

#include <robinhood/robinhood.h>

class DebugRenderer;
class GameRenderer;
struct RenderResources;

namespace Renderer
{
    class Renderer;
    class RenderGraph;
    class RenderGraphBuilder;
    class RenderGraphResources;
    class DescriptorSetResource;
}

namespace Map
{
    struct Chunk;
}

struct TerrainReserveOffsets
{
public:
    u32 chunkDataStartOffset = 0;
    u32 cellDataStartOffset = 0;
    u32 vertexDataStartOffset = 0;
};

class TerrainRenderer
{
public:
    TerrainRenderer(Renderer::Renderer* renderer, GameRenderer* gameRenderer, DebugRenderer* debugRenderer);
    ~TerrainRenderer();

    void Update(f32 deltaTime);

    void AddOccluderPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);
    void AddCullingPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);
    void AddGeometryPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);

    void Clear();
    void Reserve(u32 numChunks);
    void AllocateChunks(u32 numChunks, TerrainReserveOffsets& reserveOffsets);

    u32 AddChunk(u32 chunkHash, Map::Chunk* chunk, ivec2 chunkGridPos);
    u32 AddChunk(u32 chunkHash, Map::Chunk* chunk, ivec2 chunkGridPos, u32 chunkDataStartOffset, u32 cellDataStartOffset, u32 vertexDataStartOffset);

    void RegisterMaterialPassBufferUsage(Renderer::RenderGraphBuilder& builder);

    // Drawcall stats
    inline u32 GetNumDrawCalls() { return _instanceDatas.Count(); }
    inline u32 GetNumOccluderDrawCalls(u32 viewID) { return _numOccluderDrawCalls[viewID]; }
    inline u32 GetNumSurvivingDrawCalls(u32 viewID) { return _numSurvivingDrawCalls[viewID]; }

    // Triangle stats
    inline u32 GetNumTriangles() { return GetNumDrawCalls() * Terrain::CELL_NUM_TRIANGLES; }
    inline u32 GetNumOccluderTriangles(u32 viewID) { return GetNumOccluderDrawCalls(viewID) * Terrain::CELL_NUM_TRIANGLES; }
    inline u32 GetNumSurvivingGeometryTriangles(u32 viewID) { return GetNumSurvivingDrawCalls(viewID) * Terrain::CELL_NUM_TRIANGLES; }

private:
    void CreatePermanentResources();
    void CreatePipelines();
    void InitDescriptorSets();

    void SyncToGPU();

    struct DrawParams
    {
    public:
        bool shadowPass = false;
        u32 viewIndex = 0;
        bool cullingEnabled = false;

        Renderer::ImageMutableResource visibilityBuffer;
        Renderer::DepthImageMutableResource depth;

        Renderer::BufferResource instanceBuffer;
        Renderer::BufferResource argumentBuffer;

        Renderer::DescriptorSetResource globalDescriptorSet;
        Renderer::DescriptorSetResource drawDescriptorSet;

        u32 argumentsIndex = 0;
    };
    void Draw(const RenderResources& resources, u8 frameIndex, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList, DrawParams& params);

    struct FillDrawCallsParams
    {
    public:
        std::string passName;

        u32 cellCount;
        u32 viewIndex;
        bool diffAgainstPrev = false;

        Renderer::BufferMutableResource culledInstanceBitMaskBuffer;
        Renderer::BufferMutableResource prevCulledInstanceBitMaskBuffer;

        Renderer::DescriptorSetResource fillSet;
    };
    void FillDrawCalls(u8 frameIndex, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList, FillDrawCallsParams& params);

private:
    struct TerrainVertex
    {
    public:
        u8 normal[3] = { 0, 0, 0 }; // 3 bytes
        u8 color[3] = { 0, 0, 0 }; // 3 bytes
        u8 padding[2] = { 0, 0 }; // 2 bytes
        f32 height = 0; // 4 bytes
    };

    struct InstanceData
    {
    public:
        u32 packedChunkCellID = 0;
        u32 globalCellID = 0;
    };

    struct CellData
    {
    public:
        u16 diffuseIDs[4] = { 0, 0, 0, 0 };
        u64 hole = 0;
    };

    struct ChunkData
    {
    public:
        u32 alphaMapID = 0;
    };

    struct CellHeightRange
    {
    public:
        f32 min = 0;
        f32 max = 0;
    };

private:
    Renderer::Renderer* _renderer = nullptr;
    GameRenderer* _gameRenderer = nullptr;
    DebugRenderer* _debugRenderer = nullptr;

    Renderer::ComputePipelineID _resetIndirectBufferPipeline;
    Renderer::ComputePipelineID _fillDrawCallsPipeline;
    Renderer::ComputePipelineID _cullingPipeline;
    Renderer::GraphicsPipelineID _drawPipeline;
    Renderer::GraphicsPipelineID _drawShadowPipeline;

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
    u32 _culledInstanceBitMaskBufferSizePerView = 0;
    Renderer::BufferID _culledInstanceBuffer;

    Renderer::TextureArrayID _textures;
    Renderer::TextureArrayID _alphaTextures;

    Renderer::SamplerID _colorSampler;
    Renderer::SamplerID _alphaSampler;
    Renderer::SamplerID _occlusionSampler;

    Renderer::DescriptorSet _occluderFillPassDescriptorSet;
    Renderer::DescriptorSet _resetIndirectDescriptorSet;
    Renderer::DescriptorSet _cullingPassDescriptorSet;
    Renderer::DescriptorSet _geometryFillPassDescriptorSet;

    std::vector<Geometry::AABoundingBox> _cellBoundingBoxes;
    std::vector<Geometry::AABoundingBox> _chunkBoundingBoxes;

    std::atomic<u32> _numChunksLoaded = 0;
    std::mutex _chunkLoadMutex;
    std::mutex _chunkLoadAlphaTextureMutex;

    u32 _numOccluderDrawCalls[Renderer::Settings::MAX_VIEWS] = { 0 };
    u32 _numSurvivingDrawCalls[Renderer::Settings::MAX_VIEWS] = { 0 };
    
    std::shared_mutex _packedChunkCellIDToGlobalCellIDMutex;
    robin_hood::unordered_map<u32, u32> _packedChunkCellIDToGlobalCellID;

    std::shared_mutex _addChunkMutex; // Unique lock for operations that can reallocate, shared_lock if it only reads/modifies existing data

    friend class TerrainManipulator;
};