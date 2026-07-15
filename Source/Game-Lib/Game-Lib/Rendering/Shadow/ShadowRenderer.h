#pragma once
#include "Game-Lib/Rendering/Camera.h"

#include <Base/Types.h>
#include <Renderer/RenderSettings.h>
#include <Renderer/GPUVector.h>
#include <Renderer/Buffer.h>
#include <Renderer/DescriptorSet.h>

#include <robinhood/robinhood.h>

#include <Renderer/Descriptors/ComputePipelineDesc.h>
#include <Renderer/Descriptors/ImageDesc.h>
#include <Renderer/Descriptors/SamplerDesc.h>
#include <Renderer/Descriptors/TextureArrayDesc.h>

namespace Renderer
{
    class Renderer;
    class RenderGraph;
}

struct RenderResources;
class DebugRenderer;
class GameRenderer;
class ModelRenderer;
class TerrainRenderer;

class ShadowRenderer
{
public:
    ShadowRenderer(Renderer::Renderer* renderer, GameRenderer* gameRenderer, DebugRenderer* debugRenderer, TerrainRenderer* terrainRenderer, ModelRenderer* modelRenderer, RenderResources& resources);
    ~ShadowRenderer();

    void Update(f32 deltaTime, RenderResources& resources);

    void AddDepthMinMaxPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);
    void AddCascadeFitPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);
    void AddShadowPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);

    // SVSM (shadowTechnique 1): page marking, page table lifecycle and allocation. Runs alongside
    // the CSM path until the SVSM rendering and sampling stages land
    void AddSVSMUpdatePass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);
    void AddSVSMDebugOverlayPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);

    // Reduced scene depth bounds as view distances, false while no valid depth has been reduced yet
    bool GetDepthBoundsViewDistances(const RenderResources& resources, f32& outMinDistance, f32& outMaxDistance) const;

    // Effective cascade range after hysteresis and quantization, false while SDSM has not fitted yet
    bool GetEffectiveShadowRange(f32& outMinDistance, f32& outMaxDistance) const;

    // Fitted light-space extents per cascade, false while the XY reduction is not running
    bool GetCascadeFittedBounds(u32 cascadeIndex, vec3& outExtents, bool& outValid) const;

    struct SVSMClipmapStats
    {
        u32 marked = 0;
        u32 resident = 0;
        u32 dirty = 0;
        u32 evicted = 0;
        u32 invalidated = 0;
        u32 dynamicLive = 0;
        u32 deferred = 0;
        f32 extent = 0.0f;
    };

    // One frame old readback values, only meaningful while shadowTechnique is 1
    bool GetSVSMClipmapStats(u32 clipmapIndex, SVSMClipmapStats& outStats) const;
    void GetSVSMGlobalStats(u32& outFreePages, u32& outTotalPages, u32& outOverflow, u32& outInvalidationCause) const;
    void GetSVSMDynamicStats(u32& outLivePages, u32& outTotalPages, u32& outOverflow) const;
    u32 GetSVSMBudgetUsed() const { return _svsmDataReadBack[216]; } // SVSMDataOffsets::StatsBudgetUsed

    // Binds the cameras buffer into every SDSM/SVSM set that reads or writes it. Called at init
    // (buffer binds only reach the canonical descriptor copies at a later FlipFrame, so mid-frame
    // binds leave a hole) and again if the cameras buffer is ever recreated
    void BindCameraBuffers(RenderResources& resources);

    // Current-frame CPU knowledge: with no dynamic casters at all, the dynamic fills/draws skip entirely
    bool HasSVSMDynamicCasters() const { return !_svsmDynamicAABBs.empty(); }

    // True if the clipmap had live dynamic pages in either of the last two frames (readback is one
    // frame old, the second frame is hysteresis). Inactive views skip their dynamic fill+draw,
    // a caster entering a fresh ring renders at most one frame late
    bool GetSVSMDynamicViewActive(u32 clipmapIndex) const
    {
        return _svsmDataReadBack[196 + clipmapIndex] != 0 || _svsmDynamicLivePrev[clipmapIndex] != 0; // SVSMDataOffsets::StatsDynamicLive
    }

    // CPU-side caster classification counts, rebuilt each Update
    void GetSVSMCasterStats(u32& outDynamicCasters, u32& outAnimatedCasters, u32& outDroppedAABBs) const
    {
        outDynamicCasters = _svsmNumDynamicCasters;
        outAnimatedCasters = _svsmNumAnimatedCasters;
        outDroppedAABBs = _svsmDynamicAABBsDropped;
    }

    // SVSM resources for the terrain/model page render passes, the pools are created lazily on
    // the first shadowTechnique 1 frame
    Renderer::BufferID GetSVSMDataBuffer() const { return _svsmDataBuffer; }
    Renderer::BufferID GetSVSMPageTableBuffer() const { return _svsmPageTableBuffer; }
    Renderer::ImageID GetSVSMPagePool() const { return _svsmPagePool; }
    Renderer::BufferID GetSVSMDynamicPageTableBuffer() const { return _svsmDynamicPageTableBuffer; }
    Renderer::ImageID GetSVSMDynamicPagePool() const { return _svsmDynamicPagePool; }

    // For the material pass LIGHT set bindings, which must stay valid before the pools exist.
    // Nothing samples the placeholder while the page tables have no resident entries
    Renderer::ImageID GetSVSMPagePoolOrPlaceholder() const { return _svsmPagePool != Renderer::ImageID::Invalid() ? _svsmPagePool : _svsmPagePoolPlaceholder; }
    Renderer::ImageID GetSVSMDynamicPagePoolOrPlaceholder() const { return _svsmDynamicPagePool != Renderer::ImageID::Invalid() ? _svsmDynamicPagePool : _svsmPagePoolPlaceholder; }

private:
    void CreatePermanentResources(RenderResources& resources);

private:
    Renderer::Renderer* _renderer = nullptr;
    GameRenderer* _gameRenderer = nullptr;
    DebugRenderer* _debugRenderer = nullptr;
    TerrainRenderer* _terrainRenderer = nullptr;
    ModelRenderer* _modelRenderer = nullptr;

    Renderer::SamplerID _shadowCmpSampler;
    Renderer::SamplerID _shadowPointClampSampler;

    Renderer::ComputePipelineID _depthMinMaxPipeline;
    Renderer::DescriptorSet _depthMinMaxDescriptorSet;
    Renderer::BufferID _depthMinMaxBuffer;
    Renderer::BufferID _depthMinMaxReadBackBuffer;
    u32 _depthMinMaxReadBack[2] = { 0xFFFFFFFF, 0 };

    static constexpr u32 SDSM_DATA_FLOAT_COUNT = 8 + 8 + 8 + 4 * Renderer::Settings::MAX_SHADOW_CASCADES * 2; // SDSMState + splitDist + splitDepth + cascadeStable + cascadeDiag

    Renderer::ComputePipelineID _cascadeFitRangePipeline;
    Renderer::ComputePipelineID _cascadeXYReducePipeline;
    Renderer::ComputePipelineID _cascadeFitCamerasPipeline;
    Renderer::DescriptorSet _cascadeFitRangeDescriptorSet;
    Renderer::DescriptorSet _cascadeXYReduceDescriptorSet;
    Renderer::DescriptorSet _cascadeFitCamerasDescriptorSet;
    Renderer::BufferID _sdsmDataBuffer;
    Renderer::BufferID _cascadeBoundsBuffer;
    Renderer::BufferID _sdsmDataReadBackBuffer;
    Renderer::BufferID _cascadeCamerasReadBackBuffer;
    Camera _readBackCascadeCameras[Renderer::Settings::MAX_SHADOW_CASCADES];
    f32 _sdsmDataReadBack[SDSM_DATA_FLOAT_COUNT] = { 0.0f };

    // SVSM: scalar layout of SVSMData in Shadows/SVSM.inc.slang, offsets in ShadowRenderer.cpp.
    // Tail: clipRect{MinX,MinY,MaxX,MaxY}[24] at 220..315 (3 clip rects x 8 clipmaps)
    static constexpr u32 SVSM_DATA_UINT_COUNT = 316;
    static constexpr u32 SVSM_MAX_CLIPMAPS = 8;
    static constexpr u32 SVSM_MAX_PAGE_TABLE_SIZE = 64;   // Pages per row, buffers are sized for this cap
    static constexpr u32 SVSM_MAX_POOL_PAGES = 4096;      // Physical page index is 12 bits in the table entry
    static constexpr u32 SVSM_MAX_DIRTY_AABBS = 1024;
    static constexpr u32 SVSM_MAX_DYNAMIC_AABBS = 4096; // Animated forests are many small casters, overflow drops (never full-invalidates)

    Renderer::ComputePipelineID _svsmPreparePipeline;
    Renderer::ComputePipelineID _svsmInvalidateAABBsPipeline;
    Renderer::ComputePipelineID _svsmPageUpdateAPipeline;
    Renderer::ComputePipelineID _svsmPageMarkPipeline;
    Renderer::ComputePipelineID _svsmPageUpdateBPipeline;
    Renderer::ComputePipelineID _svsmDynamicMarkPipeline;
    Renderer::ComputePipelineID _svsmDynamicUpdatePipeline;
    Renderer::ComputePipelineID _svsmFinalizePipeline;
    Renderer::ComputePipelineID _svsmPageClearPipeline;
    Renderer::ComputePipelineID _svsmPageTableDebugPipeline;
    Renderer::ComputePipelineID _svsmPoolDebugPipeline;
    Renderer::DescriptorSet _svsmPrepareDescriptorSet;
    Renderer::DescriptorSet _svsmInvalidateAABBsDescriptorSet;
    Renderer::DescriptorSet _svsmPageUpdateADescriptorSet;
    Renderer::DescriptorSet _svsmPageMarkDescriptorSet;
    Renderer::DescriptorSet _svsmPageUpdateBDescriptorSet;
    Renderer::DescriptorSet _svsmDynamicMarkDescriptorSet;
    Renderer::DescriptorSet _svsmDynamicUpdateDescriptorSet;
    Renderer::DescriptorSet _svsmFinalizeDescriptorSet;
    Renderer::DescriptorSet _svsmPageClearDescriptorSet;
    Renderer::DescriptorSet _svsmDynamicPageClearDescriptorSet;
    Renderer::DescriptorSet _svsmPageTableDebugDescriptorSet;
    Renderer::DescriptorSet _svsmPoolDebugDescriptorSet;

    Renderer::BufferID _svsmDataBuffer;
    Renderer::BufferID _svsmPageTableBuffer;
    Renderer::BufferID _svsmFreeListBuffer;
    Renderer::BufferID _svsmDirtyAABBBuffer;
    Renderer::BufferID _svsmClearListBuffer;
    Renderer::BufferID _svsmDynamicPageTableBuffer;
    Renderer::BufferID _svsmDynamicFreeListBuffer;
    Renderer::BufferID _svsmDynamicClearListBuffer;
    Renderer::BufferID _svsmDynamicAABBBuffer;
    Renderer::BufferID _svsmDataReadBackBuffer;
    Renderer::BufferID _svsmDynamicValidateReadBackBuffer; // svsmValidateDynamic one-shot: dynamic table + free list
    Renderer::ImageID _svsmPagePool;
    Renderer::ImageID _svsmDynamicPagePool;
    Renderer::ImageID _svsmPagePoolPlaceholder;
    u32 _svsmDataReadBack[SVSM_DATA_UINT_COUNT] = { 0 };
    u32 _svsmDynamicLivePrev[SVSM_MAX_CLIPMAPS] = { 0 }; // The readback generation before the current one, for skip hysteresis

    std::vector<vec4> _svsmDirtyAABBs;   // Static invalidation (min, max) pairs, uploaded by the update pass
    std::vector<vec4> _svsmDynamicAABBs; // This frame's dynamic caster (min, max) pairs
    robin_hood::unordered_set<u32> _svsmPrevDynamicEntities; // Last frame's dynamic entity handles for transition detection
    bool _svsmDirtyAABBOverflow = false;
    u32 _svsmNumDynamicCasters = 0;   // ECS entities: moved or actively bone-simulated
    u32 _svsmNumAnimatedCasters = 0;  // In-range animated doodad instances from the ModelRenderer
    u32 _svsmDynamicAABBsDropped = 0; // Dynamic list overflow: dropped casters freeze for the frame
    bool _svsmForceInvalidateAll = false; // Set when invalidations were discarded while SVSM was inactive
    bool _svsmValidatePending = false;    // A validation copy was recorded, Update maps and checks it next frame
    bool _svsmPoolNeedsClear = false;
    u32 _svsmPoolPages = 0;
    u32 _svsmDynamicPoolPages = 0;

    f32 _lastDeltaTime = 0.0f;
};