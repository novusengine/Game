#pragma once
#include "Game-Lib/Rendering/CulledRenderer.h"

#include <Renderer/GPUVector.h>

#include <Base/Types.h>

#include <Jolt/Jolt.h>
#ifdef JPH_DEBUG_RENDERER
#include <Jolt/Renderer/DebugRenderer.h>
#endif

class DebugRenderer;
class GameRenderer;
struct RenderResources;

class JoltDebugRenderer : public CulledRenderer
#ifdef JPH_DEBUG_RENDERER
    , public JPH::DebugRenderer
#endif
{
public:
    JPH_OVERRIDE_NEW_DELETE

    JoltDebugRenderer(Renderer::Renderer* renderer, GameRenderer* gameRenderer, ::DebugRenderer* debugRenderer);

    void Update(f32 deltaTime);
    void Clear();

    void AddOccluderPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);
    void AddCullingPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);
    void AddGeometryPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);

    CullingResourcesBase& GetIndexedCullingResources() { return _indexedCullingResources; }
    CullingResourcesBase& GetCullingResources() { return _cullingResources; }

private:
#ifdef JPH_DEBUG_RENDERER
    class JoltBatch : public JPH::RefTargetVirtual, public JPH::RefTarget<JoltBatch>
    {
    public:
        JPH_OVERRIDE_NEW_DELETE

        JoltBatch();

        virtual void AddRef() override { RefTarget<JoltBatch>::AddRef(); }
        virtual void Release() override { RefTarget<JoltBatch>::Release(); }

        u32 drawID;
        bool isIndexed = false;
    };
#else
    class JoltBatch
    {
    public:
        JoltBatch();

        virtual void AddRef() {  }
        virtual void Release() { }

        u32 drawID;
        bool isIndexed = false;
    };
#endif

private:
    void CreatePermanentResources();
    void CreatePipelines();
    void InitDescriptorSets();

    void SyncToGPU();
    void Compact();

#ifdef JPH_DEBUG_RENDERER
    void DrawLine(JPH::RVec3Arg inFrom, JPH::RVec3Arg inTo, JPH::ColorArg inColor) override;
    void DrawTriangle(JPH::RVec3Arg inV1, JPH::RVec3Arg inV2, JPH::RVec3Arg inV3, JPH::ColorArg inColor, ECastShadow inCastShadow = ECastShadow::Off) override;
    
    Batch CreateTriangleBatch(const Vertex* inVertices, i32 inVertexCount, const u32* inIndices, i32 inIndexCount) override;
    Batch CreateTriangleBatch(const Triangle* inTriangles, i32 inTriangleCount) override;

    void DrawGeometry(JPH::RMat44Arg inModelMatrix, const JPH::AABox& inWorldSpaceBounds, f32 inLODScaleSq, JPH::ColorArg inModelColor, const GeometryRef& inGeometry, ECullMode inCullMode = ECullMode::CullBackFace, ECastShadow inCastShadow = ECastShadow::On, EDrawMode inDrawMode = EDrawMode::Solid) override;

    void DrawText3D(JPH::RVec3Arg inPosition, const std::string_view& inString, JPH::ColorArg inColor = JPH::Color::sWhite, f32 inHeight = 0.5f) override;
#endif

    void Draw(const RenderResources& resources, u8 frameIndex, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList, const DrawParams& params);

private:
    struct DrawManifest
    {
    public:
        vec3 center;
        vec3 extents;
        std::vector<u32> instanceIDs;
    };

    struct PackedVertex
    {
    public:
        vec4 posAndUVx; // xyz = pos, w = uv.x
        vec4 normalAndUVy; // xyz = normal, w = uv.y
        vec4 color;
    };

    // We have one of these for each draw call
    struct DrawCallData
    {
    public:
        u32 baseInstanceLookupOffset;
        u32 padding;
    };

private:
    GameRenderer* _gameRenderer = nullptr;

    CullingResourcesIndexed<DrawCallData> _indexedCullingResources;
    std::vector<DrawManifest> _indexedDrawManifests;
    
    CullingResourcesNonIndexed<DrawCallData> _cullingResources;
    std::vector<DrawManifest> _drawManifests;
    
    Renderer::GPUVector<mat4x4> _instances;

    Renderer::GPUVector<PackedVertex> _vertices;
    Renderer::GPUVector<u32> _indices;

    Renderer::GraphicsPipelineID _drawPipeline;
};
