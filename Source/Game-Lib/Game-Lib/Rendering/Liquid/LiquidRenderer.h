#pragma once
#include <Game-Lib/Rendering/CulledRenderer.h>

#include <Base/Types.h>

namespace Renderer
{
    class Renderer;
}

class DebugRenderer;

struct LiquidReserveOffsets
{
    u32 instanceStartOffset = 0;
    u32 vertexStartOffset = 0;
    u32 indexStartOffset = 0;
};

class LiquidRenderer : CulledRenderer
{
public:
    struct ReserveInfo
    {
        u32 numInstances = 0;
        u32 numVertices = 0;
        u32 numIndices = 0;
    };

private:
#pragma pack(push, 1)
    struct Vertex
    {
        u8 xCellOffset = 0;
        u8 yCellOffset = 0;
        u8 padding[2] = { 0, 0 };
        f32 height = 0.0f;
        hvec2 uv = hvec2(f16(0), f16(0));
    };

    struct DrawCallData
    {
        u16 chunkID;
        u16 cellID;
        u16 textureStartIndex;
        u8 textureCount;
        u8 hasDepth;
        u16 liquidType;
        u16 padding0;
        hvec2 uvAnim = hvec2(f16(1), f16(0)); // x seems to be scrolling, y seems to be rotation
    };

    struct Constants
    {
        Color shallowOceanColor;
        Color deepOceanColor;
        Color shallowRiverColor;
        Color deepRiverColor;
        f32 liquidVisibilityRange;
        f32 currentTime;
    };
#pragma pack(pop)

public:
    LiquidRenderer(Renderer::Renderer* renderer, DebugRenderer* debugRenderer);
    ~LiquidRenderer();

    void Update(f32 deltaTime);
    void Clear();

    void Reserve(const ReserveInfo& info, LiquidReserveOffsets& reserveOffsets);

    struct LoadDesc
    {
        u32 chunkID;
        u32 cellID;
        u8 typeID;

        u8 posX;
        u8 posY;

        u8 width;
        u8 height;

        u8 startX;
        u8 endX;

        u8 startY;
        u8 endY;

        vec2 cellPos;

        f32 defaultHeight;
        f32* heightMap = nullptr;
        u8* bitMap = nullptr;

        u32 vertexOffset;
        u32 vertexCount;
        u32 indexOffset;
        u32 indexCount;
        u32 instanceOffset;
    };
    void Load(LoadDesc& desc);

    void AddCopyDepthPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);
    void AddCullingPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);
    void AddGeometryPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);

    CullingResourcesIndexed<DrawCallData>& GetCullingResources() { return _cullingResources; }

private:
    void CreatePermanentResources();

    void SyncToGPU();

    void Draw(const RenderResources& resources, u8 frameIndex, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList, const DrawParams& params);

private:
    Renderer::Renderer* _renderer = nullptr;
    DebugRenderer* _debugRenderer = nullptr;

    Renderer::DescriptorSet _copyDescriptorSet;

    Constants _constants;

    Renderer::SamplerID _sampler;
    Renderer::SamplerID _depthCopySampler;
    Renderer::TextureArrayID _textures;

    CullingResourcesIndexed<DrawCallData> _cullingResources;
    Renderer::GPUVector<Vertex> _vertices;
    Renderer::GPUVector<u16> _indices;

    std::shared_mutex _addLiquidMutex; // Unique lock for operations that can reallocate, shared_lock if it only reads/modifies existing data
    std::mutex _textureMutex;
};