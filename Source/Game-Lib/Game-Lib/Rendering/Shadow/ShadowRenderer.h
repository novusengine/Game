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
class GameRenderer;
class ModelRenderer;
class TerrainRenderer;

class ShadowRenderer
{
public:
    ShadowRenderer(Renderer::Renderer* renderer, GameRenderer* gameRenderer, TerrainRenderer* terrainRenderer, ModelRenderer* modelRenderer, RenderResources& resources);
    ~ShadowRenderer();

    void Update(f32 deltaTime, RenderResources& resources);

    // SVSM: page marking, page table lifecycle and allocation
    void AddSVSMUpdatePass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);

    // Binds the (lazily created) page pools into the LIGHT set every frame, runs even when the
    // update pass is disabled (svsmFreeze)
    void AddSVSMBindPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);
    void AddSVSMDebugOverlayPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);

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

    // One frame old readback values
    bool GetSVSMClipmapStats(u32 clipmapIndex, SVSMClipmapStats& outStats) const;
    void GetSVSMGlobalStats(u32& outFreePages, u32& outTotalPages, u32& outOverflow, u32& outInvalidationCause) const;
    void GetSVSMDynamicStats(u32& outLivePages, u32& outTotalPages, u32& outOverflow) const;
    u32 GetSVSMBudgetUsed() const; // Defined in the .cpp next to the SVSMData mirror offsets

    // Binds the cameras buffer into every SVSM set that reads or writes it. Called at init
    // (buffer binds only reach the canonical descriptor copies at a later FlipFrame, so mid-frame
    // binds leave a hole) and again if the cameras buffer is ever recreated
    void BindCameraBuffers(RenderResources& resources);

    // Current-frame CPU knowledge: with no dynamic casters at all, the dynamic fills/draws skip
    // entirely. Per-view skipping is GPU-driven (Finalize's fill dispatch args) — a readback-based
    // per-view skip is a frame late and drops freshly acquired dynamic pages' draws for a frame
    bool HasSVSMDynamicCasters() const { return !_svsmDynamicAABBs.empty(); }

    // Shadows enabled and not night-gated: the single condition the clipmap culling and SVSM
    // geometry passes gate on (freeze deliberately excluded, frozen frames keep recording draws)
    bool IsSVSMActive() const;

    // CPU-side caster classification counts, rebuilt each Update from the ModelRenderer's
    // classifier: live set size, this frame's enter/leave transitions, and static-spilled casters
    void GetSVSMCasterStats(u32& outDynamicCasters, u32& outTransitionsIn, u32& outTransitionsOut, u32& outDroppedAABBs) const
    {
        outDynamicCasters = _svsmNumDynamicCasters;
        outTransitionsIn = _svsmCasterTransitionsIn;
        outTransitionsOut = _svsmCasterTransitionsOut;
        outDroppedAABBs = _svsmDynamicAABBsDropped;
    }

    // SVSM resources for the terrain/model page render passes, the pools are created lazily on
    // the first shadow-enabled frame
    Renderer::BufferID GetSVSMDataBuffer() const { return _svsmDataBuffer; }
    Renderer::BufferID GetSVSMPageTableBuffer() const { return _svsmPageTableBuffer; }
    Renderer::ImageID GetSVSMPagePool() const { return _svsmPagePool; }
    Renderer::BufferID GetSVSMDynamicPageTableBuffer() const { return _svsmDynamicPageTableBuffer; }
    Renderer::ImageID GetSVSMDynamicPagePool() const { return _svsmDynamicPagePool; }

    // Finalize-written per-view fill dispatch args: per clipmap 5 x uvec3 (model static fill,
    // model dynamic fill, terrain static fill, model static overhead, model dynamic overhead —
    // the overhead args gate the drawcall-granularity clear + CreateIndirect companions),
    // byte stride SVSM_FILL_ARGS_VIEW_STRIDE
    Renderer::BufferID GetSVSMFillArgsBuffer() const { return _svsmFillArgsBuffer; }
    static constexpr u32 SVSM_FILL_ARGS_VIEW_STRIDE = 5 * 3 * sizeof(u32);
    static constexpr u32 SVSM_FILL_ARGS_DYNAMIC_OFFSET = 3 * sizeof(u32);
    static constexpr u32 SVSM_FILL_ARGS_TERRAIN_OFFSET = 6 * sizeof(u32);
    static constexpr u32 SVSM_FILL_ARGS_STATIC_OVERHEAD_OFFSET = 9 * sizeof(u32);
    static constexpr u32 SVSM_FILL_ARGS_DYNAMIC_OVERHEAD_OFFSET = 12 * sizeof(u32);

    // The single clipmap-count cap every clamp site uses. The camera buffer and bitmask slices
    // are laid out against the Engine's view cap, the two must stay equal
    static constexpr u32 SVSM_MAX_CLIPMAPS = 8;
    static_assert(SVSM_MAX_CLIPMAPS == Renderer::Settings::MAX_SHADOW_CASCADES, "SVSM clipmap cap must match the Engine shadow view cap, both size the camera buffer and bitmask slices");

    // Dynamic AABB buffer capacity; the ModelRenderer's classifier caps its emission against this
    // and spills the excess to the static path (never excluded from both pools)
    static constexpr u32 SVSM_MAX_DYNAMIC_AABBS = 4096;

    // Scalar layout size of SVSMData in Shadows/SVSM.inc.slang. The .cpp mirrors the struct
    // (SVSMDataMirror) and static_asserts against this so layout drift breaks the build instead
    // of silently misreporting the readback. Tail: clipRect{MinX,MinY,MaxX,MaxY}[24] at 220..315
    static constexpr u32 SVSM_DATA_UINT_COUNT = 316;

    // For the material pass LIGHT set bindings, which must stay valid before the pools exist.
    // Nothing samples the placeholder while the page tables have no resident entries
    Renderer::ImageID GetSVSMPagePoolOrPlaceholder() const { return _svsmPagePool != Renderer::ImageID::Invalid() ? _svsmPagePool : _svsmPagePoolPlaceholder; }
    Renderer::ImageID GetSVSMDynamicPagePoolOrPlaceholder() const { return _svsmDynamicPagePool != Renderer::ImageID::Invalid() ? _svsmDynamicPagePool : _svsmPagePoolPlaceholder; }

private:
    void CreatePermanentResources(RenderResources& resources);
    void ResetSVSMPoolState(RenderResources& resources);

private:
    Renderer::Renderer* _renderer = nullptr;
    GameRenderer* _gameRenderer = nullptr;
    TerrainRenderer* _terrainRenderer = nullptr;
    ModelRenderer* _modelRenderer = nullptr;

    static constexpr u32 SVSM_MAX_PAGE_TABLE_SIZE = 64;   // Pages per row, buffers are sized for this cap
    static constexpr u32 SVSM_MAX_POOL_PAGES = 4096;      // Physical page index is 12 bits in the table entry
    static constexpr u32 SVSM_MAX_DIRTY_AABBS = 1024;

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
    Renderer::BufferID _svsmDataReadBackBuffer; // Diagnostics only (perf editor stats), no rendering decision reads it
    Renderer::BufferID _svsmDynamicValidateReadBackBuffer; // svsmValidateDynamic one-shot: dynamic table + free list
    Renderer::BufferID _svsmFillArgsBuffer;
    Renderer::ImageID _svsmPagePool;
    Renderer::ImageID _svsmDynamicPagePool;
    Renderer::ImageID _svsmPagePoolPlaceholder;
    u32 _svsmDataReadBack[SVSM_DATA_UINT_COUNT] = { 0 };

    std::vector<vec4> _svsmDirtyAABBs;   // Static invalidation (min, max) pairs, uploaded by the update pass
    std::vector<vec4> _svsmDynamicAABBs; // This frame's dynamic caster (min, max) pairs
    bool _svsmDirtyAABBOverflow = false;
    u32 _svsmNumDynamicCasters = 0;     // Classifier live set: moved or bone-pushed within the grace window
    u32 _svsmCasterTransitionsIn = 0;   // Classifier enter/leave running totals, each re-bakes static pages
    u32 _svsmCasterTransitionsOut = 0;
    u32 _svsmDynamicAABBsDropped = 0;   // Spilled to the static path this frame (cap or oversize)
    bool _svsmForceInvalidateAll = false; // Set when invalidations were discarded while SVSM was inactive
    bool _svsmValidatePending = false;    // A validation copy was recorded, Update maps and checks it next frame
    bool _svsmPoolNeedsClear = false;
    u32 _svsmPoolPages = 0;
    u32 _svsmDynamicPoolPages = 0;

    // The config ResetSVSMPoolState last shaped the free lists and page counts for. pageSize is
    // live-editable (a change refills them in place); the pool sizes are restart-only once the
    // pool images exist, the Engine cannot destroy images to recreate them at new dimensions
    u32 _svsmAppliedPageSize = 0;
    u32 _svsmAppliedPoolSize = 0;
    u32 _svsmAppliedDynamicPoolSize = 0;

    // Caster-toggle cvar states as of last Update: a flip must re-bake the whole static cache,
    // resident pages keep the toggled class's baked depth forever otherwise (marked pages never
    // age out while visible). Initialized to the cvar defaults so startup does not invalidate
    bool _svsmModelsCastShadow = true;
    bool _svsmTerrainCastShadow = true;

    // Night gate: entered after shadowStrength sits at 0 for a sustained second (the dusk
    // threshold must not flicker the cache), left immediately with a full re-bake
    bool _svsmNightActive = false;
    f32 _svsmNightTimer = 0.0f;
};