#pragma once
#include <Base/Types.h>

#include <Renderer/DescriptorSet.h>
#include <Renderer/Descriptors/SamplerDesc.h>
#include <Renderer/GPUVector.h>

#include <Base/Container/ConcurrentQueue.h>

#include <entt/fwd.hpp>
#include <robinhood/robinhood.h>

namespace Renderer
{
    class Renderer;
    class RenderGraph;

}

class DebugRenderer;
class GameRenderer;
struct RenderResources;

class LightRenderer
{
    static constexpr u32 MAX_DECALS_PER_TILE = 8;
    static constexpr u32 TILE_SIZE = 16; // in pixels, per axis (16 = 16x16 = 256 pixels per tile)
    static constexpr u32 DECAL_READBACK_FRAME_COUNT = 2;

public:
    LightRenderer(Renderer::Renderer* renderer, GameRenderer* gameRenderer, DebugRenderer* debugRenderer);
    ~LightRenderer();

    void Update(f32 deltaTime);
    void Upload();
    void Clear();

    void AddClassificationPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);
    void AddDebugPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);

    // Requires Transform, AABB and Decal components on entity
    void AddDecal(entt::entity entity);
    void RemoveDecal(entt::entity entity);

    inline u32 CalculateNumTiles(const vec2& size);
    uvec2 CalculateNumTiles2D(const vec2& size);

    void RegisterMaterialPassBufferUsage(Renderer::RenderGraphBuilder& builder);
    u32 GetDecalTileOverflowCount() const { return _decalTileOverflowCount; }

private:
    void CreatePermanentResources();
    
    void RecreateBuffer(const vec2& size);

public:
    enum DecalFlags
    {
        DECAL_FLAG_TWOSIDED = 1 << 0,
    };

    struct DecalAddRequest
    {
        entt::entity entity;
    };

    struct DecalRemoveRequest
    {
        entt::entity entity;
    };

private:
    struct GPUDecal
    {
        vec4 positionAndTextureID; // xyz = position, w = texture index
        quat rotation;
        vec4 extentsAndColor; // xyz = extents, asuint(w) = uint color multiplier
        hvec2 thresholdMinMax = hvec2(0.0f, 1.0f);
        hvec2 minUV = hvec2(0.0f, 0.0f);
        hvec2 maxUV = hvec2(1.0f, 1.0f);
        u32 flags;
        f32 opacity;
        i32 priority;
        u32 stableID;
        u32 reserved = 0;
    };
    static_assert(sizeof(GPUDecal) == 80);

private:
    Renderer::Renderer* _renderer;

    Renderer::DescriptorSet _classifyPassDescriptorSet;
    Renderer::DescriptorSet _debugPassDescriptorSet;

    Renderer::GPUVector<GPUDecal> _decals;
    robin_hood::unordered_map<u32, entt::entity> _decalIDToEntity;
    robin_hood::unordered_map<entt::entity, u32> _entityToDecalID;

    Renderer::BufferID _entityTilesBuffer;
    Renderer::BufferID _decalOverflowReadbacks[DECAL_READBACK_FRAME_COUNT] = {};
    bool _hasDecalOverflowReadback[DECAL_READBACK_FRAME_COUNT] = {};
    u32 _decalTileOverflowCount = 0;
    u32 _nextStableDecalID = 0;

    Renderer::ComputePipelineID _classificationPipeline;

    moodycamel::ConcurrentQueue<DecalAddRequest> _decalAddRequests;
    std::vector<DecalAddRequest> _decalAddWork;

    moodycamel::ConcurrentQueue<DecalRemoveRequest> _decalRemoveRequests;
    std::vector<DecalRemoveRequest> _decalRemoveWork;

    GameRenderer* _gameRenderer = nullptr;
    DebugRenderer* _debugRenderer = nullptr;
};
