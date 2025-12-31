#pragma once
#include "Game-Lib/Rendering/CullingResources.h"

#include <Base/Types.h>
#include <Base/Math/Geometry.h>

#include <FileFormat/Shared.h>
#include <FileFormat/Novus/Model/ComplexModel.h>

#include <Renderer/RenderGraphBuilder.h>
#include <Renderer/DescriptorSetResource.h>
#include <Renderer/FrameResource.h>
#include <Renderer/GPUVector.h>

class DebugRenderer;
class GameRenderer;
struct RenderResources;

namespace Renderer
{
    class Renderer;
    class RenderGraph;
    class RenderGraphResources;
    class DescriptorSetResource;
    class CommandList;
}

struct DrawParams;

class CulledRenderer
{
public:
    void InitCullingResources(CullingResourcesBase& resources);

protected:
    struct DrawParams
    {
    public:
        bool cullingEnabled = false;
        bool shadowPass = false;
        u32 viewIndex = 0;

        CullingResourcesBase* cullingResources;
        
        Renderer::ImageMutableResource rt0;
        Renderer::ImageMutableResource rt1;
        Renderer::DepthImageMutableResource depth;

        Renderer::BufferMutableResource argumentBuffer;
        Renderer::BufferMutableResource drawCountBuffer;

        std::vector<Renderer::DescriptorSetResource*> descriptorSets;

        u32 drawCountIndex = 0;
        u32 numMaxDrawCalls = 0;
    };

    CulledRenderer(Renderer::Renderer* renderer, GameRenderer* gameRenderer, DebugRenderer* debugRenderer);
    ~CulledRenderer();

    void Update(f32 deltaTime);
    void Clear();

    struct PassParams
    {
        std::string passName = "";

        Renderer::RenderGraphResources* graphResources;
        Renderer::CommandList* commandList;
        CullingResourcesBase* cullingResources;
        
        u8 frameIndex;
    };

    template <typename Data>
    void OccluderPassSetup(Data& data, Renderer::RenderGraphBuilder& builder, CullingResourcesIndexedBase* cullingResources, u8 frameIndex)
    {
        using BufferUsage = Renderer::BufferPassUsage;

        data.culledDrawCallsBuffer = builder.Write(cullingResources->GetCulledDrawsBuffer(), BufferUsage::GRAPHICS | BufferUsage::COMPUTE);
        data.culledDrawCallsBitMaskBuffer = builder.Write(cullingResources->GetCulledDrawCallsBitMaskBuffer(!frameIndex), BufferUsage::TRANSFER | BufferUsage::GRAPHICS | BufferUsage::COMPUTE);
        data.prevCulledDrawCallsBitMaskBuffer = builder.Write(cullingResources->GetCulledDrawCallsBitMaskBuffer(frameIndex), BufferUsage::TRANSFER | BufferUsage::GRAPHICS | BufferUsage::COMPUTE);

        data.drawCountBuffer = builder.Write(cullingResources->GetDrawCountBuffer(), BufferUsage::TRANSFER | BufferUsage::GRAPHICS | BufferUsage::COMPUTE);
        data.triangleCountBuffer = builder.Write(cullingResources->GetTriangleCountBuffer(), BufferUsage::TRANSFER | BufferUsage::GRAPHICS | BufferUsage::COMPUTE);
        data.drawCountReadBackBuffer = builder.Write(cullingResources->GetOccluderDrawCountReadBackBuffer(), BufferUsage::TRANSFER);
        data.triangleCountReadBackBuffer = builder.Write(cullingResources->GetOccluderTriangleCountReadBackBuffer(), BufferUsage::TRANSFER);

        builder.Read(cullingResources->GetDrawCalls().GetBuffer(), BufferUsage::GRAPHICS | BufferUsage::COMPUTE);
        builder.Read(cullingResources->GetInstanceRefs().GetBuffer(), BufferUsage::GRAPHICS | BufferUsage::COMPUTE);

        builder.Write(cullingResources->GetCulledInstanceLookupTableBuffer(), BufferUsage::COMPUTE | BufferUsage::GRAPHICS);

        data.occluderFillSet = builder.Use(cullingResources->GetOccluderFillDescriptorSet());
        data.createIndirectDescriptorSet = builder.Use(cullingResources->GetCreateIndirectAfterCullDescriptorSet());
        data.drawSet = builder.Use(cullingResources->GetGeometryPassDescriptorSet());
    }

    template <typename Data>
    void OccluderPassSetup(Data& data, Renderer::RenderGraphBuilder& builder, CullingResourcesNonIndexedBase* cullingResources, u8 frameIndex)
    {
        using BufferUsage = Renderer::BufferPassUsage;

        data.culledDrawCallsBuffer = builder.Write(cullingResources->GetCulledDrawsBuffer(), BufferUsage::GRAPHICS | BufferUsage::COMPUTE);
        data.culledDrawCallsBitMaskBuffer = builder.Write(cullingResources->GetCulledDrawCallsBitMaskBuffer(!frameIndex), BufferUsage::TRANSFER | BufferUsage::GRAPHICS | BufferUsage::COMPUTE);
        data.prevCulledDrawCallsBitMaskBuffer = builder.Write(cullingResources->GetCulledDrawCallsBitMaskBuffer(frameIndex), BufferUsage::TRANSFER | BufferUsage::GRAPHICS | BufferUsage::COMPUTE);

        data.drawCountBuffer = builder.Write(cullingResources->GetDrawCountBuffer(), BufferUsage::TRANSFER | BufferUsage::GRAPHICS | BufferUsage::COMPUTE);
        data.triangleCountBuffer = builder.Write(cullingResources->GetTriangleCountBuffer(), BufferUsage::TRANSFER | BufferUsage::GRAPHICS | BufferUsage::COMPUTE);
        data.drawCountReadBackBuffer = builder.Write(cullingResources->GetOccluderDrawCountReadBackBuffer(), BufferUsage::TRANSFER);
        data.triangleCountReadBackBuffer = builder.Write(cullingResources->GetOccluderTriangleCountReadBackBuffer(), BufferUsage::TRANSFER);

        builder.Read(cullingResources->GetDrawCalls().GetBuffer(), BufferUsage::GRAPHICS | BufferUsage::COMPUTE);
        builder.Read(cullingResources->GetInstanceRefs().GetBuffer(), BufferUsage::GRAPHICS | BufferUsage::COMPUTE);

        builder.Write(cullingResources->GetCulledInstanceLookupTableBuffer(), BufferUsage::COMPUTE | BufferUsage::GRAPHICS);

        data.occluderFillSet = builder.Use(cullingResources->GetOccluderFillDescriptorSet());
        data.createIndirectDescriptorSet = builder.Use(cullingResources->GetCullingDescriptorSet());
        data.drawSet = builder.Use(cullingResources->GetGeometryPassDescriptorSet());
    }

    struct OccluderPassParams : public PassParams
    {
    public:
        Renderer::ImageMutableResource rt0;
        Renderer::ImageMutableResource rt1;
        Renderer::DepthImageMutableResource depth[Renderer::Settings::MAX_VIEWS];

        Renderer::BufferMutableResource culledDrawCallsBuffer;
        Renderer::BufferMutableResource culledDrawCallCountBuffer;

        Renderer::BufferMutableResource culledDrawCallsBitMaskBuffer;
        Renderer::BufferMutableResource prevCulledDrawCallsBitMaskBuffer;

        Renderer::BufferMutableResource culledInstanceCountsBuffer;
        
        Renderer::BufferMutableResource drawCountBuffer;
        Renderer::BufferMutableResource triangleCountBuffer;
        Renderer::BufferMutableResource drawCountReadBackBuffer;
        Renderer::BufferMutableResource triangleCountReadBackBuffer;

        Renderer::DescriptorSetResource globalDescriptorSet;
        Renderer::DescriptorSetResource occluderFillDescriptorSet;
        Renderer::DescriptorSetResource createIndirectDescriptorSet;
        Renderer::DescriptorSetResource drawDescriptorSet;

        std::function<void(DrawParams&)> drawCallback;

        u32 baseInstanceLookupOffset = 0;
        u32 drawCallDataSize = 0;

        u32 numCascades = 0;

        f32 biasConstantFactor = 0.0f;
        f32 biasClamp = 0.0f;
        f32 biasSlopeFactor = 0.0f;

        bool enableDrawing = false; // Allows us to do everything but the actual drawcall, for debugging
        bool disableTwoStepCulling = false;
    };
    void OccluderPass(OccluderPassParams& params);

    template <typename Data>
    void CullingPassSetup(Data& data, Renderer::RenderGraphBuilder& builder, CullingResourcesIndexedBase* cullingResources, u8 frameIndex)
    {
        using BufferUsage = Renderer::BufferPassUsage;

        data.culledDrawCallsBuffer = builder.Write(cullingResources->GetCulledDrawsBuffer(), BufferUsage::COMPUTE);
        data.culledInstanceCountsBuffer = builder.Write(cullingResources->GetCulledInstanceCountsBuffer(), BufferUsage::TRANSFER | BufferUsage::COMPUTE);
        data.culledDrawCallCountBuffer = builder.Write(cullingResources->GetCulledDrawCallCountBuffer(), BufferUsage::TRANSFER | BufferUsage::COMPUTE);
        builder.Write(cullingResources->GetCulledInstanceLookupTableBuffer(), BufferUsage::COMPUTE);

        data.drawCountBuffer = builder.Write(cullingResources->GetDrawCountBuffer(), BufferUsage::TRANSFER | BufferUsage::COMPUTE);
        data.triangleCountBuffer = builder.Write(cullingResources->GetTriangleCountBuffer(), BufferUsage::TRANSFER | BufferUsage::COMPUTE);
        data.drawCountReadBackBuffer = builder.Write(cullingResources->GetDrawCountReadBackBuffer(), BufferUsage::TRANSFER);
        data.triangleCountReadBackBuffer = builder.Write(cullingResources->GetTriangleCountReadBackBuffer(), BufferUsage::TRANSFER);

        builder.Read(cullingResources->GetDrawCalls().GetBuffer(), BufferUsage::COMPUTE);
        builder.Read(cullingResources->GetInstanceRefs().GetBuffer(), BufferUsage::COMPUTE);

        data.cullingSet = builder.Use(cullingResources->GetCullingDescriptorSet());
        data.createIndirectAfterCullSet = builder.Use(cullingResources->GetCreateIndirectAfterCullDescriptorSet());
    }

    template <typename Data>
    void CullingPassSetup(Data& data, Renderer::RenderGraphBuilder& builder, CullingResourcesNonIndexedBase* cullingResources, u8 frameIndex)
    {
        using BufferUsage = Renderer::BufferPassUsage;

        data.prevCulledDrawCallsBitMask = builder.Read(cullingResources->GetCulledDrawCallsBitMaskBuffer(!frameIndex), BufferUsage::COMPUTE);
        data.currentCulledDrawCallsBitMask = builder.Write(cullingResources->GetCulledDrawCallsBitMaskBuffer(frameIndex), BufferUsage::COMPUTE);
        data.culledDrawCallsBuffer = builder.Write(cullingResources->GetCulledDrawsBuffer(), BufferUsage::COMPUTE);
        data.culledInstanceCountsBuffer = builder.Write(cullingResources->GetCulledInstanceCountsBuffer(), BufferUsage::TRANSFER | BufferUsage::COMPUTE);
        data.culledDrawCallCountBuffer = builder.Write(cullingResources->GetCulledDrawCallCountBuffer(), BufferUsage::TRANSFER | BufferUsage::COMPUTE);
        builder.Write(cullingResources->GetCulledInstanceLookupTableBuffer(), BufferUsage::COMPUTE);

        data.drawCountBuffer = builder.Write(cullingResources->GetDrawCountBuffer(), BufferUsage::TRANSFER | BufferUsage::COMPUTE);
        data.triangleCountBuffer = builder.Write(cullingResources->GetTriangleCountBuffer(), BufferUsage::TRANSFER | BufferUsage::COMPUTE);
        data.drawCountReadBackBuffer = builder.Write(cullingResources->GetDrawCountReadBackBuffer(), BufferUsage::TRANSFER);
        data.triangleCountReadBackBuffer = builder.Write(cullingResources->GetTriangleCountReadBackBuffer(), BufferUsage::TRANSFER);

        builder.Read(cullingResources->GetDrawCalls().GetBuffer(), BufferUsage::COMPUTE);
        builder.Read(cullingResources->GetInstanceRefs().GetBuffer(), BufferUsage::COMPUTE);

        data.cullingSet = builder.Use(cullingResources->GetCullingDescriptorSet());
    }

    struct CullingPassParams : public PassParams
    {
    public:
        Renderer::ImageResource depthPyramid;

        Renderer::BufferResource prevCulledDrawCallsBitMask;

        Renderer::BufferMutableResource currentCulledDrawCallsBitMask;
        Renderer::BufferMutableResource culledInstanceCountsBuffer;
        Renderer::BufferMutableResource culledDrawCallsBuffer;
        Renderer::BufferMutableResource culledDrawCallCountBuffer;

        Renderer::BufferMutableResource drawCountBuffer;
        Renderer::BufferMutableResource triangleCountBuffer;
        Renderer::BufferMutableResource drawCountReadBackBuffer;
        Renderer::BufferMutableResource triangleCountReadBackBuffer;

        Renderer::DescriptorSetResource debugDescriptorSet;
        Renderer::DescriptorSetResource globalDescriptorSet;
        Renderer::DescriptorSetResource cullingDescriptorSet;
        Renderer::DescriptorSetResource createIndirectAfterCullSet;

        u32 numCascades = 0;
        bool occlusionCull = true;
        bool disableTwoStepCulling = false;

        bool modelIDIsDrawCallID = false;
        bool cullingDataIsWorldspace = false;
        bool debugDrawColliders = false;

        u32 instanceIDOffset = 0;
        u32 modelIDOffset = 0;
        u32 baseInstanceLookupOffset = 0;
        u32 drawCallDataSize = 0;
    };
    void CullingPass(CullingPassParams& params);

    template <typename Data>
    void GeometryPassSetup(Data& data, Renderer::RenderGraphBuilder& builder, CullingResourcesIndexedBase* cullingResources, u8 frameIndex)
    {
        using BufferUsage = Renderer::BufferPassUsage;

        data.drawCallsBuffer = builder.Write(cullingResources->GetDrawCalls().GetBuffer(), BufferUsage::GRAPHICS | BufferUsage::COMPUTE);
        data.culledDrawCallsBuffer = builder.Write(cullingResources->GetCulledDrawsBuffer(), BufferUsage::GRAPHICS | BufferUsage::COMPUTE);
        data.culledDrawCallCountBuffer = builder.Write(cullingResources->GetCulledDrawCallCountBuffer(), BufferUsage::GRAPHICS);

        data.drawCountBuffer = builder.Write(cullingResources->GetDrawCountBuffer(), BufferUsage::TRANSFER | BufferUsage::GRAPHICS | BufferUsage::COMPUTE);
        data.triangleCountBuffer = builder.Write(cullingResources->GetTriangleCountBuffer(), BufferUsage::TRANSFER | BufferUsage::GRAPHICS | BufferUsage::COMPUTE);
        data.drawCountReadBackBuffer = builder.Write(cullingResources->GetDrawCountReadBackBuffer(), BufferUsage::TRANSFER);
        data.triangleCountReadBackBuffer = builder.Write(cullingResources->GetTriangleCountReadBackBuffer(), BufferUsage::TRANSFER);

        builder.Read(cullingResources->GetInstanceRefs().GetBuffer(), BufferUsage::GRAPHICS);
        builder.Read(cullingResources->GetCulledInstanceLookupTableBuffer(), BufferUsage::GRAPHICS);

        data.drawSet = builder.Use(cullingResources->GetGeometryPassDescriptorSet());
    }

    template <typename Data>
    void GeometryPassSetup(Data& data, Renderer::RenderGraphBuilder& builder, CullingResourcesNonIndexedBase* cullingResources, u8 frameIndex)
    {
        using BufferUsage = Renderer::BufferPassUsage;

        data.drawCallsBuffer = builder.Write(cullingResources->GetDrawCalls().GetBuffer(), BufferUsage::GRAPHICS);
        data.culledDrawCallsBuffer = builder.Write(cullingResources->GetCulledDrawsBuffer(), BufferUsage::GRAPHICS);
        data.culledDrawCallCountBuffer = builder.Write(cullingResources->GetCulledDrawCallCountBuffer(), BufferUsage::GRAPHICS);

        data.drawCountBuffer = builder.Write(cullingResources->GetDrawCountBuffer(), BufferUsage::TRANSFER | BufferUsage::GRAPHICS);
        data.triangleCountBuffer = builder.Write(cullingResources->GetTriangleCountBuffer(), BufferUsage::TRANSFER | BufferUsage::GRAPHICS);
        data.drawCountReadBackBuffer = builder.Write(cullingResources->GetDrawCountReadBackBuffer(), BufferUsage::TRANSFER);
        data.triangleCountReadBackBuffer = builder.Write(cullingResources->GetTriangleCountReadBackBuffer(), BufferUsage::TRANSFER);

        builder.Read(cullingResources->GetInstanceRefs().GetBuffer(), BufferUsage::GRAPHICS);
        builder.Read(cullingResources->GetCulledInstanceLookupTableBuffer(), BufferUsage::GRAPHICS);

        data.drawSet = builder.Use(cullingResources->GetGeometryPassDescriptorSet());
    }

    struct GeometryPassParams : public PassParams
    {
    public:
        Renderer::ImageMutableResource rt0;
        Renderer::ImageMutableResource rt1;
        Renderer::DepthImageMutableResource depth[Renderer::Settings::MAX_VIEWS];

        Renderer::BufferMutableResource drawCallsBuffer;
        Renderer::BufferMutableResource culledDrawCallsBuffer;

        Renderer::BufferMutableResource culledDrawCallsBitMaskBuffer;
        Renderer::BufferMutableResource prevCulledDrawCallsBitMaskBuffer;

        Renderer::BufferMutableResource culledDrawCallCountBuffer;

        Renderer::BufferMutableResource drawCountBuffer;
        Renderer::BufferMutableResource triangleCountBuffer;
        Renderer::BufferMutableResource drawCountReadBackBuffer;
        Renderer::BufferMutableResource triangleCountReadBackBuffer;

        Renderer::DescriptorSetResource globalDescriptorSet;
        Renderer::DescriptorSetResource fillDescriptorSet;
        Renderer::DescriptorSetResource drawDescriptorSet;

        std::function<void(DrawParams&)> drawCallback;

        u32 numCascades = 0;

        f32 biasConstantFactor = 0.0f;
        f32 biasClamp = 0.0f;
        f32 biasSlopeFactor = 0.0f;

        bool enableDrawing = false; // Allows us to do everything but the actual drawcall, for debugging
        bool cullingEnabled = false;
    };
    void GeometryPass(GeometryPassParams& params);

    void SyncToGPU();
    void BindCullingResource(CullingResourcesBase& resources);

private:
    void CreatePermanentResources();
    void CreatePipelines();

protected:
    Renderer::Renderer* _renderer = nullptr;
    GameRenderer* _gameRenderer = nullptr;
    DebugRenderer* _debugRenderer = nullptr;

    Renderer::GPUVector<Model::ComplexModel::CullingData> _cullingDatas;

    static bool _pipelinesCreated;
    static Renderer::ComputePipelineID _fillInstancedDrawCallsFromBitmaskPipeline[2]; // [0] = non-indexed, [1] = indexed
    static Renderer::ComputePipelineID _fillDrawCallsFromBitmaskPipeline[2]; // [0] = non-indexed, [1] = indexed
    static Renderer::ComputePipelineID _createIndirectAfterCullingPipeline[2]; // [0] = non-indexed, [1] = indexed
    static Renderer::ComputePipelineID _createIndirectAfterCullingOrderedPipeline[2]; // [0] = non-indexed, [1] = indexed
    static Renderer::ComputePipelineID _cullingInstancedPipeline[2]; // [0] = no bitmasks, [1] = use bitmasks
    static Renderer::ComputePipelineID _cullingPipeline[2]; // [0] = no bitmasks, [1] = use bitmasks

    Renderer::SamplerID _occlusionSampler;
};