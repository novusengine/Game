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
        bool svsmPass = false;        // Attachment-less page render, needs an explicit render area
        bool svsmDynamicPass = false; // Caster-split dynamic instances into the dynamic pool
        u32 svsmRectIndex = 0xFFFFFFFFu; // Clip rect this draw renders (0-2), SVSM_CLIP_RECT_DISABLED = no clipping
        u32 viewIndex = 0;

        CullingResourcesBase* cullingResources;

        Renderer::ImageMutableResource rt0;
        Renderer::ImageMutableResource rt1;
        Renderer::DepthImageMutableResource depth;
        uvec2 svsmExtent = uvec2(0, 0);

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
        builder.Write(cullingResources->GetCulledDrawCallsBitMaskBuffer(frameIndex), BufferUsage::TRANSFER | BufferUsage::GRAPHICS | BufferUsage::COMPUTE);

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
        builder.Write(cullingResources->GetCulledDrawCallsBitMaskBuffer(frameIndex), BufferUsage::TRANSFER | BufferUsage::GRAPHICS | BufferUsage::COMPUTE);

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
        Renderer::DepthImageMutableResource depth;

        Renderer::BufferMutableResource culledDrawCallsBuffer;
        Renderer::BufferMutableResource culledDrawCallCountBuffer;

        Renderer::BufferMutableResource culledDrawCallsBitMaskBuffer;

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

        builder.Write(cullingResources->GetCulledDrawCallsBitMaskBuffer(!frameIndex), BufferUsage::COMPUTE); // Write because both bitmask bindings are RW in the shader
        builder.Write(cullingResources->GetCulledDrawCallsBitMaskBuffer(frameIndex), BufferUsage::COMPUTE);
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

        u32 numShadowViews = 0;
        bool cullMainView = true;
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
        data.culledDrawCallCountBuffer = builder.Write(cullingResources->GetCulledDrawCallCountBuffer(), BufferUsage::TRANSFER | BufferUsage::GRAPHICS | BufferUsage::COMPUTE);

        data.drawCountBuffer = builder.Write(cullingResources->GetDrawCountBuffer(), BufferUsage::TRANSFER | BufferUsage::GRAPHICS | BufferUsage::COMPUTE);
        data.triangleCountBuffer = builder.Write(cullingResources->GetTriangleCountBuffer(), BufferUsage::TRANSFER | BufferUsage::GRAPHICS | BufferUsage::COMPUTE);
        data.drawCountReadBackBuffer = builder.Write(cullingResources->GetDrawCountReadBackBuffer(), BufferUsage::TRANSFER);
        data.triangleCountReadBackBuffer = builder.Write(cullingResources->GetTriangleCountReadBackBuffer(), BufferUsage::TRANSFER);

        builder.Read(cullingResources->GetInstanceRefs().GetBuffer(), BufferUsage::GRAPHICS | BufferUsage::COMPUTE);
        builder.Write(cullingResources->GetCulledInstanceLookupTableBuffer(), BufferUsage::COMPUTE | BufferUsage::GRAPHICS);

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

        Renderer::BufferMutableResource culledDrawCallCountBuffer;

        Renderer::BufferMutableResource culledInstanceCountsBuffer; // Instanced only, used to rebuild per-cascade draw sets

        Renderer::BufferMutableResource drawCountBuffer;
        Renderer::BufferMutableResource triangleCountBuffer;
        Renderer::BufferMutableResource drawCountReadBackBuffer;
        Renderer::BufferMutableResource triangleCountReadBackBuffer;

        Renderer::DescriptorSetResource globalDescriptorSet;
        Renderer::DescriptorSetResource fillDescriptorSet;
        Renderer::DescriptorSetResource createIndirectDescriptorSet; // Instanced only
        Renderer::DescriptorSetResource drawDescriptorSet;

        std::function<void(DrawParams&)> drawCallback;

        u32 baseInstanceLookupOffset = 0; // Instanced only
        u32 drawCallDataSize = 0; // Instanced only

        u32 firstViewIndex = 0; // 0 = main view first, 1 = clipmap views only
        u32 numShadowViews = 0;

        bool enableDrawing = false; // Allows us to do everything but the actual drawcall, for debugging
        bool cullingEnabled = false;

        bool svsmPass = false;         // SVSM page render: no depth targets, attachment-less draws into svsmExtent
        uvec2 svsmExtent = uvec2(0, 0);

        // SVSM caster split: per view, fill+draw static instances (drawCallback) then dynamic
        // instances (drawCallbackDynamic), filtered by the dynamic instance mask
        bool svsmSplitFills = false;
        std::function<void(DrawParams&)> drawCallbackDynamic;
        Renderer::BufferMutableResource svsmDynamicDrawCountReadBackBuffer; // Per-view dynamic surviving counts for the perf editor

        // Finalize-written per-view fill dispatch args: rings with no dirty static pages / no
        // resident dynamic pages this frame get zero-group fills. Same-frame GPU truth — a
        // CPU/readback gate here is a frame late and flickers freshly acquired dynamic pages
        Renderer::BufferResource svsmFillArgsBuffer; // Finalize-written dispatch args, consumed read-only via DispatchIndirect

        // Byte layout of svsmFillArgsBuffer (the ShadowRenderer's stride/offset constants),
        // passed in by the pass owner so this shared base never depends on ShadowRenderer
        u32 svsmFillArgsViewStride = 0;
        u32 svsmFillArgsDynamicOffset = 0;
        u32 svsmFillArgsStaticOverheadOffset = 0;
        u32 svsmFillArgsDynamicOverheadOffset = 0;
    };
    void GeometryPass(GeometryPassParams& params);
    void RunInstancedGeometryFill(GeometryPassParams& params, u32 viewIndex, bool filtered, bool keepDynamic, const std::string& fillMarkerName, const std::string& createIndirectMarkerName); // Shared-buffer rebuild from a view's bitmask slice; markers prebuilt by the caller, this runs per view
    void ClipmapCullingPass(CullingPassParams& params); // Instanced-only frustum cull of cascade views, no occlusion, no counter resets

    // The instanced cull dispatch shared by CullingPass and ClipmapCullingPass: one thread per
    // instance against the per-view frustums, writing bitmask slices and instance counts.
    // occlusionCull/cullMainView/debugDrawColliders come from params; the clipmap pass overrides
    // them before calling
    void RunInstancedCullingDispatch(CullingPassParams& params, bool useBitmasks, bool bindDepthPyramid);

    // The bitmask-driven instanced fill dispatch shared by OccluderPass (last frame's main-view
    // bits) and RunInstancedGeometryFill (this frame's per-view slices, optionally gated through
    // Finalize-written indirect args)
    void DispatchInstancedFill(PassParams& params, const std::string& markerName, Renderer::DescriptorSetResource& fillSet, bool filtered, u32 currentBitmaskIndex, u32 bitmaskOffset, bool keepDynamic, u32 baseInstanceLookupOffset, u32 drawCallDataSize, Renderer::BufferResource indirectArgsBuffer, u32 indirectArgsByteOffset);

    // The CreateIndirectAfterCulling dispatch shared by the occluder, culling and geometry-fill
    // paths: per-drawcall instance counts become indirect draw args (each count is cleared after
    // consumption, keeping the counts buffer always-zero between uses). debugSet is optional.
    // Both helpers take the full marker string prebuilt so per-view callers pay it once
    void DispatchCreateIndirect(PassParams& params, const std::string& markerName, Renderer::DescriptorSetResource& createIndirectSet, Renderer::DescriptorSetResource* debugSet, u32 baseInstanceLookupOffset, u32 drawCallDataSize, Renderer::BufferResource indirectArgsBuffer, u32 indirectArgsByteOffset);

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
    static Renderer::ComputePipelineID _fillInstancedDrawCallsFilteredPipeline[2]; // Same, with the SVSM dynamic-mask filter
    static Renderer::ComputePipelineID _fillDrawCallsFromBitmaskPipeline[2]; // [0] = non-indexed, [1] = indexed
    static Renderer::ComputePipelineID _createIndirectAfterCullingPipeline[2]; // [0] = non-indexed, [1] = indexed
    static Renderer::ComputePipelineID _createIndirectAfterCullingOrderedPipeline[2]; // [0] = non-indexed, [1] = indexed
    static Renderer::ComputePipelineID _cullingInstancedPipeline[2]; // [0] = no bitmasks, [1] = use bitmasks
    static Renderer::ComputePipelineID _cullingPipeline[2]; // [0] = no bitmasks, [1] = use bitmasks

    Renderer::SamplerID _occlusionSampler;
};