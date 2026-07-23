#include "ShadowRenderer.h"
#include <Game-Lib/Application/EnttRegistries.h>
#include <Game-Lib/ECS/Components/AABB.h>
#include <Game-Lib/ECS/Components/Camera.h>
#include <Game-Lib/ECS/Components/Model.h>
#include <Game-Lib/ECS/Singletons/DayNightCycle.h>
#include <Game-Lib/ECS/Util/Transforms.h>

#include <Game-Lib/ECS/Systems/UpdateAreaLights.h>
#include <Game-Lib/Rendering/GameRenderer.h>
#include <Game-Lib/Rendering/Terrain/TerrainRenderer.h>
#include <Game-Lib/Rendering/Model/ModelRenderer.h>
#include <Game-Lib/Rendering/RenderResources.h>
#include <Game-Lib/Rendering/Camera.h>
#include <Game-Lib/Util/ServiceLocator.h>

#include <Renderer/Descriptors/ComputeShaderDesc.h>

#include <Base/CVarSystem/CVarSystem.h>

#include <Renderer/Renderer.h>
#include <Renderer/RenderGraph.h>

#include <entt/entt.hpp>

#include <bit>
#include <cstddef>

AutoCVar_Int CVAR_ShadowEnabled(CVarCategory::Client | CVarCategory::Rendering, "shadowEnabled", "enable shadows", 1, CVarFlags::EditCheckbox);
AutoCVar_Float CVAR_ShadowStrength(CVarCategory::Client | CVarCategory::Rendering, "shadowStrength", "directional shadow strength, overwritten each frame from the sun elevation", 1.0f, CVarFlags::EditReadOnly);
AutoCVar_Float CVAR_ShadowNormalOffsetBias(CVarCategory::Client | CVarCategory::Rendering, "shadowNormalOffsetBias", "receiver offset along the surface normal in shadow texels, fights acne on hard angles", 1.0f);
AutoCVar_Float CVAR_ShadowCasterMargin(CVarCategory::Client | CVarCategory::Rendering, "shadowCasterMargin", "extends clipmap culling toward the sun so far-away casters with long shadows are not culled, depth clamp pancakes them onto the near plane", 2500.0f);
AutoCVar_Int CVAR_SVSMNumClipmaps(CVarCategory::Client | CVarCategory::Rendering, "svsmNumClipmaps", "number of SVSM clipmap rings, each ring doubles the covered area", 6);
AutoCVar_Float CVAR_SVSMClipmap0Extent(CVarCategory::Client | CVarCategory::Rendering, "svsmClipmap0Extent", "world extent of the finest SVSM clipmap window in meters, finer rings multiply the near-field page demand", 64.0f);
AutoCVar_Int CVAR_SVSMVirtualSize(CVarCategory::Client | CVarCategory::Rendering, "svsmVirtualSize", "virtual texture resolution per clipmap", 8192);
AutoCVar_Int CVAR_SVSMPageSize(CVarCategory::Client | CVarCategory::Rendering, "svsmPageSize", "texels per page", 128);
AutoCVar_Int CVAR_SVSMPoolSize(CVarCategory::Client | CVarCategory::Rendering, "svsmPoolSize", "physical page pool texture resolution, restart-only once shadows have been enabled", 8192);
AutoCVar_Int CVAR_SVSMPageEvictAge(CVarCategory::Client | CVarCategory::Rendering, "svsmPageEvictAge", "frames without a visible sample before a cached page returns to the pool, the age counter caps at 254", 240);
AutoCVar_Float CVAR_SVSMMarkBorderTexels(CVarCategory::Client | CVarCategory::Rendering, "svsmMarkBorderTexels", "filter footprint margin in texels, samples near a page border also mark the neighbor page", 4.0f);
AutoCVar_Float CVAR_SVSMResolutionScale(CVarCategory::Client | CVarCategory::Rendering, "svsmResolutionScale", "eye-distance clipmap floor: skip rings finer than the sample's screen footprint times this, 0 disables, lower = sharper distant shadows for more pages. Below ~0.25 the pool pressure-evicts and churns", 1.0f);
AutoCVar_Int CVAR_SVSMDynamicSplit(CVarCategory::Client | CVarCategory::Rendering, "svsmDynamicSplit", "static/dynamic caster split: animated and moving casters render into a transient dynamic pool instead of churning the static cache, 0 reverts to v1 behavior", 1, CVarFlags::EditCheckbox);
AutoCVar_Int CVAR_SVSMDynamicPoolSize(CVarCategory::Client | CVarCategory::Rendering, "svsmDynamicPoolSize", "dynamic page pool texture resolution, restart-only once shadows have been enabled", 2048);
AutoCVar_Int CVAR_SVSMRenderBudget(CVarCategory::Client | CVarCategory::Rendering, "svsmRenderBudget", "static pages rendered per frame, 0 = unlimited; overflow refines over following frames coarse-to-fine, the coarsest two rings are exempt", 0);
AutoCVar_Float CVAR_SVSMAnimatedCasterRange(CVarCategory::Client | CVarCategory::Rendering, "svsmAnimatedCasterRange", "camera range in meters within which animated doodads (windmills, flags) cast dynamic shadows, beyond it their pose bakes static, 0 disables", 128.0f);
AutoCVar_Int CVAR_SVSMFreeze(CVarCategory::Client | CVarCategory::Rendering, "svsmFreeze", "freeze SVSM page marking and lifecycle to inspect the cached state. Stale dirty state stays live and pages never clear, so dynamic content ghost-accumulates while frozen; unfreezing re-bakes the cache if anything spawned/despawned meanwhile", 0, CVarFlags::EditCheckbox);
AutoCVar_Int CVAR_SVSMInvalidateAll(CVarCategory::Client | CVarCategory::Rendering, "svsmInvalidateAll", "one shot, invalidate every cached SVSM page", 0, CVarFlags::EditCheckbox);
AutoCVar_Int CVAR_SVSMValidateDynamic(CVarCategory::Client | CVarCategory::Rendering, "svsmValidateDynamic", "one shot, read back the dynamic page table and free list and validate pool invariants (aliasing, leaks)", 0, CVarFlags::EditCheckbox);
AutoCVar_Int CVAR_SVSMDebugClipmap(CVarCategory::Client | CVarCategory::Rendering, "svsmDebugClipmap", "draw this clipmap's page table as an overlay, -1 disables", -1);
AutoCVar_Int CVAR_SVSMDebugShowPool(CVarCategory::Client | CVarCategory::Rendering, "svsmDebugShowPool", "draw a downsampled view of a physical page pool as an overlay: 1 = static pool, 2 = dynamic pool", 0);
AutoCVar_Float CVAR_SVSMZHalfRange(CVarCategory::Client | CVarCategory::Rendering, "svsmZHalfRange", "half depth range of the clipmap windows around the camera in light space, changes invalidate all pages", 2048.0f);
AutoCVar_Float CVAR_SVSMConstantBias(CVarCategory::Client | CVarCategory::Rendering, "svsmConstantBias", "SVSM compare bias toward the sun in world meters, the software depth path has no hardware bias", 0.15f);
AutoCVar_Int CVAR_SVSMProfileGeometry(CVarCategory::Client | CVarCategory::Rendering, "svsmProfileGeometry", "debug: per-view fill/draw GPU time queries in the SVSM geometry passes, shown in the render pass list", 0, CVarFlags::EditCheckbox);
AutoCVar_Int CVAR_SVSMClipRects(CVarCategory::Client | CVarCategory::Rendering, "svsmClipRects", "clip the static page draws to the classified dirty rects (3 draws per view), 0 reverts to one unclipped draw for A/B", 1, CVarFlags::EditCheckbox);
AutoCVar_Int CVAR_SVSMNightGate(CVarCategory::Client | CVarCategory::Rendering, "svsmNightGate", "skip all SVSM update/render work while the sun is below the horizon (shadow strength 0), dawn resumes with a full re-bake", 1, CVarFlags::EditCheckbox);

// CPU mirror of SVSMData in Shadows/SVSM.inc.slang (scalar arrays only, so std430 matches this
// exactly). Never read as a struct: it exists so the readback offsets below are compiler-derived
// and the size tripwire fires when the two sides drift instead of the readback silently
// misreporting. Insert/rename fields here in lockstep with the Slang struct
struct SVSMDataMirror
{
    f32 prevLightDirection[4];

    f32 zRangeMin;
    f32 zRangeMax;
    u32 frameInvalidationFlags;
    u32 padding0;

    i32 anchorPageMinX[8];
    i32 anchorPageMinY[8];
    i32 prevAnchorPageMinX[8];
    i32 prevAnchorPageMinY[8];
    f32 camMinusWindowMinX[8];
    f32 camMinusWindowMinY[8];
    f32 extent[8];
    f32 pageWorld[8];
    u32 clipmapInvalidate[8];

    i32 dirtyRectMinX[8];
    i32 dirtyRectMinY[8];
    i32 dirtyRectMaxX[8];
    i32 dirtyRectMaxY[8];

    u32 statsMarked[8];
    u32 statsResident[8];
    u32 statsDirty[8];
    u32 statsEvicted[8];
    u32 statsInvalidated[8];
    u32 statsOverflow;
    u32 statsInvalidationCause;
    u32 statsFreeListCount;

    u32 configPageTableSize;
    u32 configPageSize;
    u32 configPoolPagesPerRow;
    u32 configNumClipmaps;
    f32 configMarkBorderTexels;
    f32 configPadding0;
    f32 configCamMinusZAnchor;
    f32 configResolutionScale;
    u32 padding3;

    i32 dynamicRectMinX[8];
    i32 dynamicRectMinY[8];
    i32 dynamicRectMaxX[8];
    i32 dynamicRectMaxY[8];

    u32 statsDynamicLive[8];
    u32 statsDynamicOverflow;
    u32 statsDynamicTotal;
    u32 configDynamicPoolPagesPerRow;
    u32 padding4;

    u32 statsDeferred[8];
    u32 statsBudgetUsed;
    u32 padding5;
    u32 padding6;
    u32 padding7;

    i32 clipRectMinX[24];
    i32 clipRectMinY[24];
    i32 clipRectMaxX[24];
    i32 clipRectMaxY[24];
};
static_assert(sizeof(SVSMDataMirror) == ShadowRenderer::SVSM_DATA_UINT_COUNT * sizeof(u32), "SVSMDataMirror drifted from SVSMData in Shadows/SVSM.inc.slang, update both sides and the uint count together");

// u32 indices into the flat SVSMData readback, derived from the mirror so they cannot drift
// from the layout independently
namespace SVSMDataOffsets
{
    constexpr u32 Extent = offsetof(SVSMDataMirror, extent) / sizeof(u32);
    constexpr u32 StatsMarked = offsetof(SVSMDataMirror, statsMarked) / sizeof(u32);
    constexpr u32 StatsResident = offsetof(SVSMDataMirror, statsResident) / sizeof(u32);
    constexpr u32 StatsDirty = offsetof(SVSMDataMirror, statsDirty) / sizeof(u32);
    constexpr u32 StatsEvicted = offsetof(SVSMDataMirror, statsEvicted) / sizeof(u32);
    constexpr u32 StatsInvalidated = offsetof(SVSMDataMirror, statsInvalidated) / sizeof(u32);
    constexpr u32 StatsOverflow = offsetof(SVSMDataMirror, statsOverflow) / sizeof(u32);
    constexpr u32 StatsInvalidationCause = offsetof(SVSMDataMirror, statsInvalidationCause) / sizeof(u32);
    constexpr u32 StatsFreeListCount = offsetof(SVSMDataMirror, statsFreeListCount) / sizeof(u32);
    constexpr u32 StatsDynamicLive = offsetof(SVSMDataMirror, statsDynamicLive) / sizeof(u32);
    constexpr u32 StatsDynamicOverflow = offsetof(SVSMDataMirror, statsDynamicOverflow) / sizeof(u32);
    constexpr u32 StatsDynamicTotal = offsetof(SVSMDataMirror, statsDynamicTotal) / sizeof(u32);
    constexpr u32 StatsDeferred = offsetof(SVSMDataMirror, statsDeferred) / sizeof(u32);
    constexpr u32 StatsBudgetUsed = offsetof(SVSMDataMirror, statsBudgetUsed) / sizeof(u32);
}

// Page table entry bits, mirrors Shadows/SVSM.inc.slang
namespace SVSMPageEntry
{
    constexpr u32 PhysMask = 0xFFFu; // SVSM_PAGE_PHYS_MASK
    constexpr u32 Resident = 1u << 25; // SVSM_PAGE_RESIDENT
}

// SVSM invalidation cause bits, shared with Shadows/SVSM.inc.slang
namespace SVSMCause
{
    constexpr u32 Manual = 4;
    constexpr u32 AABBOverflow = 8;
}

// pageTableSize must be a power of two in [16, maxPageTableSize] (WrapPageSlot masks with
// pageTableSize - 1), and every consumer — the geometry passes' viewport extents included —
// derives it from these cvars, so invalid values are corrected at the source. Returns true when
// anything was corrected: the pre-snap values were live for a few frames and mis-addressed the
// cached pages, so the caller must invalidate the cache
static bool SanitizeSVSMConfigCVars(u32 maxPageTableSize)
{
    const u32 pageSize = static_cast<u32>(glm::max(CVAR_SVSMPageSize.Get(), 16));
    const u32 virtualSize = static_cast<u32>(glm::max(CVAR_SVSMVirtualSize.Get(), 1));

    const u32 pageTableSize = glm::clamp(std::bit_floor(virtualSize / pageSize), 16u, maxPageTableSize);
    const u32 correctedVirtualSize = pageTableSize * pageSize;

    bool corrected = false;
    if (correctedVirtualSize != static_cast<u32>(CVAR_SVSMVirtualSize.Get()))
    {
        NC_LOG_WARNING("SVSM: svsmVirtualSize {0} is not a power-of-two multiple of the page size in [16, {1}] pages, corrected to {2}", CVAR_SVSMVirtualSize.Get(), maxPageTableSize, correctedVirtualSize);
        CVAR_SVSMVirtualSize.Set(static_cast<i32>(correctedVirtualSize));
        corrected = true;
    }
    if (pageSize != static_cast<u32>(CVAR_SVSMPageSize.Get()))
    {
        NC_LOG_WARNING("SVSM: svsmPageSize {0} is below the 16 texel minimum, corrected to {1}", CVAR_SVSMPageSize.Get(), pageSize);
        CVAR_SVSMPageSize.Set(static_cast<i32>(pageSize));
        corrected = true;
    }
    return corrected;
}

// The derived page/pool geometry every consumer must agree on: the update pass constants, the
// debug overlay and the pool state reset all read this instead of re-deriving it from the cvars
struct SVSMDerivedConfig
{
    u32 pageSize = 0;
    u32 pageTableSize = 0;
    u32 poolPagesPerRow = 0;
    u32 dynamicPoolPagesPerRow = 0;
};

static SVSMDerivedConfig DeriveSVSMConfig(u32 maxPageTableSize)
{
    SVSMDerivedConfig config;
    config.pageSize = static_cast<u32>(glm::max(CVAR_SVSMPageSize.Get(), 16));
    config.pageTableSize = glm::clamp(static_cast<u32>(CVAR_SVSMVirtualSize.Get()) / config.pageSize, 16u, maxPageTableSize);
    config.poolPagesPerRow = glm::min(static_cast<u32>(CVAR_SVSMPoolSize.Get()) / config.pageSize, maxPageTableSize);
    config.dynamicPoolPagesPerRow = glm::min(static_cast<u32>(CVAR_SVSMDynamicPoolSize.Get()) / config.pageSize, maxPageTableSize);
    return config;
}

ShadowRenderer::ShadowRenderer(Renderer::Renderer* renderer, GameRenderer* gameRenderer, TerrainRenderer* terrainRenderer, ModelRenderer* modelRenderer, RenderResources& resources)
    : _renderer(renderer)
    , _gameRenderer(gameRenderer)
    , _terrainRenderer(terrainRenderer)
    , _modelRenderer(modelRenderer)
    , _svsmPrepareDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS)
    , _svsmInvalidateAABBsDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS)
    , _svsmPageUpdateADescriptorSet(Renderer::DescriptorSetSlot::PER_PASS)
    , _svsmPageMarkDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS)
    , _svsmPageUpdateBDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS)
    , _svsmDynamicMarkDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS)
    , _svsmDynamicUpdateDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS)
    , _svsmFinalizeDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS)
    , _svsmPageClearDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS)
    , _svsmDynamicPageClearDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS)
    , _svsmPageTableDebugDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS)
    , _svsmPoolDebugDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS)
{
    ZoneScoped;
    CreatePermanentResources(resources);
}

ShadowRenderer::~ShadowRenderer()
{
}

void ShadowRenderer::Update(f32 deltaTime, RenderResources& resources)
{
    ZoneScoped;

    if (SanitizeSVSMConfigCVars(SVSM_MAX_PAGE_TABLE_SIZE))
    {
        _svsmForceInvalidateAll = true; // Pages cached under the pre-snap addressing are garbage
    }

    // Live config edits: a pageSize change reshapes the pool page counts and free lists in place
    // (the pool textures keep their dimensions, only the page grid over them changes). The pool
    // size cvars are restart-only once the pool images exist — the Engine cannot destroy images,
    // so recreating them at new dimensions would leak the old texture — and revert with a warning
    {
        if (_svsmPagePool != Renderer::ImageID::Invalid() &&
            (static_cast<u32>(CVAR_SVSMPoolSize.Get()) != _svsmAppliedPoolSize || static_cast<u32>(CVAR_SVSMDynamicPoolSize.Get()) != _svsmAppliedDynamicPoolSize))
        {
            NC_LOG_WARNING("SVSM: svsmPoolSize/svsmDynamicPoolSize changes need a restart once the pools exist, reverting to {0}/{1}", _svsmAppliedPoolSize, _svsmAppliedDynamicPoolSize);
            CVAR_SVSMPoolSize.Set(static_cast<i32>(_svsmAppliedPoolSize));
            CVAR_SVSMDynamicPoolSize.Set(static_cast<i32>(_svsmAppliedDynamicPoolSize));
        }

        const bool configChanged = static_cast<u32>(glm::max(CVAR_SVSMPageSize.Get(), 16)) != _svsmAppliedPageSize
            || static_cast<u32>(CVAR_SVSMPoolSize.Get()) != _svsmAppliedPoolSize
            || static_cast<u32>(CVAR_SVSMDynamicPoolSize.Get()) != _svsmAppliedDynamicPoolSize;
        if (configChanged)
        {
            ResetSVSMPoolState(resources);
            _svsmForceInvalidateAll = true;
            if (_svsmPagePool != Renderer::ImageID::Invalid())
            {
                _svsmPoolNeedsClear = true; // The old page grid's depth is garbage under the new one
            }
        }
    }

    // Caster-toggle transitions re-bake the whole static cache: resident pages hold the toggled
    // class's baked depth, and marked pages never age out while visible, so without this the old
    // shadows persist indefinitely after a toggle
    {
        CVarSystem* cvarSystem = CVarSystem::Get();
        const bool modelsCastShadow = *cvarSystem->GetIntCVar(CVarCategory::Client | CVarCategory::Rendering, "shadowModelsCastShadow"_h) == 1;
        const bool terrainCastShadow = *cvarSystem->GetIntCVar(CVarCategory::Client | CVarCategory::Rendering, "shadowTerrainCastShadow"_h) == 1;
        if (modelsCastShadow != _svsmModelsCastShadow || terrainCastShadow != _svsmTerrainCastShadow)
        {
            _svsmModelsCastShadow = modelsCastShadow;
            _svsmTerrainCastShadow = terrainCastShadow;
            _svsmForceInvalidateAll = true;
        }
    }

    // Night gate: below the horizon the material pass multiplies shadows to nothing
    // (shadowStrength 0, and lightInfo.y already stops the sampler), so the whole producer side
    // can sleep. Enter after a sustained second so the dusk threshold can't flicker the cache;
    // leave immediately with a full re-bake — the sun moved all night, the cache is garbage at
    // dawn regardless
    {
        if (CVAR_SVSMNightGate.Get() != 0 && CVAR_ShadowStrength.GetFloat() <= 0.0f)
        {
            _svsmNightTimer += deltaTime;
            if (_svsmNightTimer > 1.0f)
            {
                _svsmNightActive = true;
            }
        }
        else
        {
            if (_svsmNightActive)
            {
                _svsmForceInvalidateAll = true;
            }
            _svsmNightActive = false;
            _svsmNightTimer = 0.0f;
        }
    }

    // SVSM: last frame's page table stats plus this frame's caster bounds. The ModelRenderer's
    // per-instance classifier owns the dynamic set (moved or bone-pushed within the grace window)
    // and its transition invalidations; this just copies the results and uploads. With the split
    // off, the classifier feeds everything through static invalidation like v1
    _svsmDirtyAABBs.clear();
    _svsmDynamicAABBs.clear();
    _svsmDirtyAABBOverflow = false;
    _svsmNumDynamicCasters = 0;
    _svsmCasterTransitionsIn = 0;
    _svsmCasterTransitionsOut = 0;
    _svsmDynamicAABBsDropped = 0;
    const bool frozen = CVAR_SVSMFreeze.Get() != 0;
    if (CVAR_ShadowEnabled.Get() && !frozen && !_svsmNightActive)
    {
        // The pools are the SVSM VRAM cost, only allocated once shadows are actually used
        if (_svsmPagePool == Renderer::ImageID::Invalid())
        {
            Renderer::ImageDesc poolDesc;
            poolDesc.debugName = "SVSMPagePool";
            poolDesc.dimensions = vec2(CVAR_SVSMPoolSize.Get(), CVAR_SVSMPoolSize.Get());
            poolDesc.dimensionType = Renderer::ImageDimensionType::DIMENSION_ABSOLUTE;
            poolDesc.format = Renderer::ImageFormat::R32_UINT;
            poolDesc.sampleCount = Renderer::SampleCount::SAMPLE_COUNT_1;
            poolDesc.clearUInts = uvec4(0, 0, 0, 0);

            _svsmPagePool = _renderer->CreateImage(poolDesc);

            poolDesc.debugName = "SVSMDynamicPagePool";
            poolDesc.dimensions = vec2(CVAR_SVSMDynamicPoolSize.Get(), CVAR_SVSMDynamicPoolSize.Get());
            _svsmDynamicPagePool = _renderer->CreateImage(poolDesc);

            _svsmPoolNeedsClear = true; // Fresh VRAM is garbage that must never be sampled, zero both once
        }

        // Diagnostics only (perf editor stats), rendering decisions never read this — it is a
        // frame old, the GPU gates its own per-view work through Finalize's fill dispatch args.
        // Single-buffered across frames in flight: this map can race the in-flight GPU copy, so
        // values may tear (house readback pattern, accepted — stats can read inconsistent)
        u32* svsmData = static_cast<u32*>(_renderer->MapBuffer(_svsmDataReadBackBuffer));
        if (svsmData != nullptr)
        {
            memcpy(_svsmDataReadBack, svsmData, sizeof(u32) * SVSM_DATA_UINT_COUNT);
        }
        _renderer->UnmapBuffer(_svsmDataReadBackBuffer);

        // svsmValidateDynamic one-shot: check the snapshotted dynamic pool invariants — every
        // physical page referenced by at most one resident entry, free-list entries unique and
        // unreferenced, resident + free == pool. Aliasing here is the ghost-shadow failure mode
        if (_svsmValidatePending)
        {
            const u32 tableUints = SVSM_MAX_CLIPMAPS * SVSM_MAX_PAGE_TABLE_SIZE * SVSM_MAX_PAGE_TABLE_SIZE;
            const u32* validateData = static_cast<const u32*>(_renderer->MapBuffer(_svsmDynamicValidateReadBackBuffer));
            if (validateData != nullptr)
            {
                const u32* freeListData = validateData + tableUints;
                const u32 poolPages = _svsmDynamicPoolPages;
                const u32 rawFreeCount = freeListData[0];
                const u32 freeCount = glm::min(rawFreeCount, SVSM_MAX_POOL_PAGES);

                std::vector<u8> physRefCounts(poolPages, 0);
                u32 numResident = 0;
                u32 numAliasedRefs = 0;
                u32 numBadPhys = 0;
                for (u32 i = 0; i < tableUints; i++)
                {
                    u32 entry = validateData[i];
                    if (entry == 0 || (entry & SVSMPageEntry::Resident) == 0)
                        continue;

                    numResident++;
                    u32 physicalPage = entry & SVSMPageEntry::PhysMask;
                    if (physicalPage >= poolPages)
                    {
                        numBadPhys++;
                        continue;
                    }
                    if (++physRefCounts[physicalPage] > 1)
                    {
                        numAliasedRefs++;
                    }
                }

                std::vector<u8> freeSeen(poolPages, 0);
                u32 numBadFree = 0;
                u32 numFreeDuplicates = 0;
                u32 numFreeButReferenced = 0;
                for (u32 i = 0; i < freeCount; i++)
                {
                    u32 physicalPage = freeListData[4 + i];
                    if (physicalPage >= poolPages)
                    {
                        numBadFree++;
                        continue;
                    }
                    if (freeSeen[physicalPage]++ != 0)
                    {
                        numFreeDuplicates++;
                    }
                    else if (physRefCounts[physicalPage] != 0)
                    {
                        numFreeButReferenced++;
                    }
                }

                bool leaked = numResident + rawFreeCount != poolPages;
                if (numAliasedRefs != 0 || numBadPhys != 0 || numBadFree != 0 || numFreeDuplicates != 0 || numFreeButReferenced != 0 || leaked)
                {
                    NC_LOG_ERROR("SVSM dynamic pool validation FAILED: {0} resident + {1} free of {2} pages | {3} aliased refs, {4} bad phys, {5} free dups, {6} free-but-referenced, {7} bad free entries", numResident, rawFreeCount, poolPages, numAliasedRefs, numBadPhys, numFreeDuplicates, numFreeButReferenced, numBadFree);
                }
                else
                {
                    NC_LOG_INFO("SVSM dynamic pool validation OK: {0} resident + {1} free = {2} pages", numResident, rawFreeCount, poolPages);
                }
            }
            _renderer->UnmapBuffer(_svsmDynamicValidateReadBackBuffer);
            _svsmValidatePending = false;
            CVAR_SVSMValidateDynamic.Set(0);
        }

        // Spawned/despawned/re-modeled instances and the classifier's transition/spill
        // invalidations always re-bake cached static pages
        if (_modelRenderer->DrainShadowInvalidations(_svsmDirtyAABBs, SVSM_MAX_DIRTY_AABBS) > SVSM_MAX_DIRTY_AABBS)
        {
            _svsmDirtyAABBOverflow = true;
        }

        // The dynamic caster set — moved or bone-pushed within the grace window — is classified
        // per instance by the ModelRenderer (one classifier feeds both this list and the GPU
        // instance mask, so they can never disagree). Capped and oversize-spilled at the source,
        // the copy just has to fit
        const std::vector<vec4>& dynamicAABBs = _modelRenderer->GetDynamicCasterAABBs();
        NC_ASSERT(dynamicAABBs.size() <= SVSM_MAX_DYNAMIC_AABBS * 2, "SVSM: ModelRenderer emitted more dynamic caster AABBs than the buffer holds, the source cap is broken");
        _svsmDynamicAABBs.assign(dynamicAABBs.begin(), dynamicAABBs.end());

        _svsmNumDynamicCasters = _modelRenderer->GetNumDynamicCasters();
        _svsmDynamicAABBsDropped = _modelRenderer->GetNumDynamicCastersDropped();
        _modelRenderer->GetDynamicCasterTransitions(_svsmCasterTransitionsIn, _svsmCasterTransitionsOut);

        // Upload this frame's AABB lists through the frame-synced staging ring, the render graph
        // issues a global upload barrier before any pass executes
        if (!_svsmDirtyAABBs.empty())
        {
            size_t uploadBytes = sizeof(vec4) * _svsmDirtyAABBs.size();
            auto uploadBuffer = _renderer->CreateUploadBuffer(_svsmDirtyAABBBuffer, 0, uploadBytes);
            memcpy(uploadBuffer->mappedMemory, _svsmDirtyAABBs.data(), uploadBytes);
        }
        if (!_svsmDynamicAABBs.empty())
        {
            size_t uploadBytes = sizeof(vec4) * _svsmDynamicAABBs.size();
            auto uploadBuffer = _renderer->CreateUploadBuffer(_svsmDynamicAABBBuffer, 0, uploadBytes);
            memcpy(uploadBuffer->mappedMemory, _svsmDynamicAABBs.data(), uploadBytes);
        }
    }
    else
    {
        // Shadows off, frozen or night-gated (the update pass doesn't run, so nothing would
        // consume the queue): spawn/despawn and classifier-transition invalidations would
        // accumulate unboundedly. Discard them (maxPairs 0 appends nothing) and re-bake the whole
        // cache on resume instead — the classifier keeps ticking meanwhile, so its state stays
        // current
        if (_modelRenderer->DrainShadowInvalidations(_svsmDirtyAABBs, 0) > 0)
        {
            _svsmForceInvalidateAll = true;
        }
    }
}

bool ShadowRenderer::IsSVSMActive() const
{
    return CVAR_ShadowEnabled.Get() != 0 && !_svsmNightActive;
}

struct ShadowRenderer::SVSMUpdatePassData
{
    Renderer::DepthImageResource depth;

    Renderer::BufferMutableResource cameras;
    Renderer::BufferMutableResource svsmDataBuffer;
    Renderer::BufferMutableResource pageTableBuffer;
    Renderer::BufferMutableResource freeListBuffer;
    Renderer::BufferResource dirtyAABBBuffer;
    Renderer::BufferMutableResource clearListBuffer;
    Renderer::BufferMutableResource dynamicPageTableBuffer;
    Renderer::BufferMutableResource dynamicFreeListBuffer;
    Renderer::BufferMutableResource dynamicClearListBuffer;
    Renderer::BufferResource dynamicAABBBuffer;
    Renderer::BufferMutableResource svsmDataReadBackBuffer;
    Renderer::BufferMutableResource dynamicValidateReadBackBuffer;
    Renderer::ImageMutableResource pagePool;
    Renderer::ImageMutableResource dynamicPagePool;

    Renderer::DescriptorSetResource prepareSet;
    Renderer::DescriptorSetResource invalidateSet;
    Renderer::DescriptorSetResource updateASet;
    Renderer::DescriptorSetResource markSet;
    Renderer::DescriptorSetResource updateBSet;
    Renderer::DescriptorSetResource dynamicMarkSet;
    Renderer::DescriptorSetResource dynamicUpdateSet;
    Renderer::DescriptorSetResource finalizeSet;
    Renderer::DescriptorSetResource clearSet;
    Renderer::DescriptorSetResource dynamicClearSet;

    bool DeclareResources(ShadowRenderer& owner, RenderResources& resources, Renderer::RenderGraphBuilder& builder)
    {
        using BufferUsage = Renderer::BufferPassUsage;

        depth = builder.Read(resources.depth, Renderer::PipelineType::COMPUTE);
        cameras = builder.Write(resources.cameras.GetBuffer(), BufferUsage::COMPUTE);
        svsmDataBuffer = builder.Write(owner._svsmDataBuffer, BufferUsage::COMPUTE | BufferUsage::TRANSFER);
        pageTableBuffer = builder.Write(owner._svsmPageTableBuffer, BufferUsage::COMPUTE);
        freeListBuffer = builder.Write(owner._svsmFreeListBuffer, BufferUsage::COMPUTE);
        dirtyAABBBuffer = builder.Read(owner._svsmDirtyAABBBuffer, BufferUsage::COMPUTE);
        clearListBuffer = builder.Write(owner._svsmClearListBuffer, BufferUsage::COMPUTE);
        dynamicPageTableBuffer = builder.Write(owner._svsmDynamicPageTableBuffer, BufferUsage::COMPUTE | BufferUsage::TRANSFER);
        dynamicFreeListBuffer = builder.Write(owner._svsmDynamicFreeListBuffer, BufferUsage::COMPUTE | BufferUsage::TRANSFER);
        dynamicClearListBuffer = builder.Write(owner._svsmDynamicClearListBuffer, BufferUsage::COMPUTE);
        dynamicAABBBuffer = builder.Read(owner._svsmDynamicAABBBuffer, BufferUsage::COMPUTE);
        svsmDataReadBackBuffer = builder.Write(owner._svsmDataReadBackBuffer, BufferUsage::TRANSFER);
        dynamicValidateReadBackBuffer = builder.Write(owner._svsmDynamicValidateReadBackBuffer, BufferUsage::TRANSFER);
        builder.Write(owner._svsmFillArgsBuffer, BufferUsage::COMPUTE);
        pagePool = builder.Write(owner._svsmPagePool, Renderer::PipelineType::COMPUTE, Renderer::LoadMode::LOAD);
        dynamicPagePool = builder.Write(owner._svsmDynamicPagePool, Renderer::PipelineType::COMPUTE, Renderer::LoadMode::LOAD);

        prepareSet = builder.Use(owner._svsmPrepareDescriptorSet);
        invalidateSet = builder.Use(owner._svsmInvalidateAABBsDescriptorSet);
        updateASet = builder.Use(owner._svsmPageUpdateADescriptorSet);
        markSet = builder.Use(owner._svsmPageMarkDescriptorSet);
        updateBSet = builder.Use(owner._svsmPageUpdateBDescriptorSet);
        dynamicMarkSet = builder.Use(owner._svsmDynamicMarkDescriptorSet);
        dynamicUpdateSet = builder.Use(owner._svsmDynamicUpdateDescriptorSet);
        finalizeSet = builder.Use(owner._svsmFinalizeDescriptorSet);
        clearSet = builder.Use(owner._svsmPageClearDescriptorSet);
        dynamicClearSet = builder.Use(owner._svsmDynamicPageClearDescriptorSet);

        return true;
    }
};

struct ShadowRenderer::SVSMUpdateRecorder
{
    struct Constants
    {
        vec4 lightDirection;
        u32 numClipmaps;
        u32 pageTableSize;
        u32 pageSize;
        u32 poolPagesPerRow;
        f32 clipmap0Extent;
        f32 markBorderTexels;
        u32 evictAge;
        u32 invalidateAll;
        u32 numDirtyAABBs;
        u32 allocClipmap;
        f32 casterMargin;
        f32 zHalfRange;
        u32 fillDrawCallCount;
        f32 padding1;
        f32 resolutionScale;
        u32 dynamicPoolPagesPerRow;
        u32 renderBudget;
        u32 dynamicPhase;
        u32 fillInstanceCount;
        u32 fillCellCount;
    };

    ShadowRenderer& owner;
    SVSMUpdatePassData& data;
    Renderer::RenderGraphResources& graphResources;
    Renderer::CommandList& commandList;
    u8 frameIndex;
    u32 numClipmaps;
    u32 numDirtyAABBs;
    u32 numDynamicAABBs;
    bool dynamicSplit;
    SVSMDerivedConfig config;
    u32 tableCapacity;
    Constants* constants;

    SVSMUpdateRecorder(ShadowRenderer& owner, SVSMUpdatePassData& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList,
        u8 frameIndex, u32 numClipmaps, const vec3& lightDirection, u32 invalidateCause, u32 numDirtyAABBs, bool dynamicSplit, u32 numDynamicAABBs);

    void Record();
    void ClearPhysicalPoolsIfNeeded();
    void PrepareFrameStateAndClipmapAnchors();
    void InvalidatePagesTouchedByChangedCasters();
    void RecycleStaleStaticPages();
    void MarkPagesNeededByVisibleReceivers();
    void AllocateAndQueueStaticPages();
    void RebuildDynamicPageSet();
    void MarkPagesTouchedByDynamicCasters();
    void BuildClipmapCamerasAndGeometryDispatches();
    void ClearPagesQueuedForRendering();
    void CaptureDiagnostics();
};

ShadowRenderer::SVSMUpdateRecorder::SVSMUpdateRecorder(ShadowRenderer& owner, SVSMUpdatePassData& data,
    Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList, u8 frameIndex, u32 numClipmaps,
    const vec3& lightDirection, u32 invalidateCause, u32 numDirtyAABBs, bool dynamicSplit, u32 numDynamicAABBs)
    : owner(owner)
    , data(data)
    , graphResources(graphResources)
    , commandList(commandList)
    , frameIndex(frameIndex)
    , numClipmaps(numClipmaps)
    , numDirtyAABBs(numDirtyAABBs)
    , numDynamicAABBs(numDynamicAABBs)
    , dynamicSplit(dynamicSplit)
    , config(DeriveSVSMConfig(SVSM_MAX_PAGE_TABLE_SIZE))
    , tableCapacity(SVSM_MAX_CLIPMAPS * SVSM_MAX_PAGE_TABLE_SIZE * SVSM_MAX_PAGE_TABLE_SIZE)
    , constants(graphResources.FrameNew<Constants>())
{
    constants->lightDirection = vec4(lightDirection, 0.0f);
    constants->numClipmaps = numClipmaps;
    constants->pageTableSize = config.pageTableSize;
    constants->pageSize = config.pageSize;
    constants->poolPagesPerRow = config.poolPagesPerRow;
    constants->clipmap0Extent = CVAR_SVSMClipmap0Extent.GetFloat();
    constants->markBorderTexels = CVAR_SVSMMarkBorderTexels.GetFloat();
    constants->evictAge = static_cast<u32>(glm::clamp(CVAR_SVSMPageEvictAge.Get(), 1, 254));
    constants->invalidateAll = invalidateCause;
    constants->numDirtyAABBs = numDirtyAABBs;
    constants->allocClipmap = 0;
    constants->casterMargin = CVAR_ShadowCasterMargin.GetFloat();
    constants->zHalfRange = CVAR_SVSMZHalfRange.GetFloat();
    constants->padding1 = 0.0f;
    constants->resolutionScale = CVAR_SVSMResolutionScale.GetFloat();
    constants->dynamicPoolPagesPerRow = dynamicSplit ? config.dynamicPoolPagesPerRow : 0;
    constants->renderBudget = static_cast<u32>(glm::max(CVAR_SVSMRenderBudget.Get(), 0));
    constants->dynamicPhase = 0;

    // Finalize turns these record-time counts into same-frame per-view indirect dispatch args.
    constants->fillInstanceCount = owner._modelRenderer->GetOpaqueCullingResources().GetNumInstances();
    constants->fillCellCount = owner._terrainRenderer->GetNumDrawCalls();
    constants->fillDrawCallCount = owner._modelRenderer->GetOpaqueCullingResources().GetDrawCallCount();
}

void ShadowRenderer::SVSMUpdateRecorder::Record()
{
    GPU_SCOPED_PROFILER_ZONE(commandList, SVSMUpdate);

    ClearPhysicalPoolsIfNeeded();
    PrepareFrameStateAndClipmapAnchors();
    InvalidatePagesTouchedByChangedCasters();
    RecycleStaleStaticPages();
    MarkPagesNeededByVisibleReceivers();
    AllocateAndQueueStaticPages();

    if (dynamicSplit)
    {
        RebuildDynamicPageSet();
    }

    BuildClipmapCamerasAndGeometryDispatches();
    ClearPagesQueuedForRendering();
    CaptureDiagnostics();
}

void ShadowRenderer::SVSMUpdateRecorder::ClearPhysicalPoolsIfNeeded()
{
    if (!owner._svsmPoolNeedsClear)
        return;

    // Fresh VRAM must be zero before sampling or reversed-depth atomics can preserve garbage.
    commandList.Clear(data.pagePool, uvec4(0, 0, 0, 0));
    commandList.ImageBarrier(data.pagePool);
    commandList.Clear(data.dynamicPagePool, uvec4(0, 0, 0, 0));
    commandList.ImageBarrier(data.dynamicPagePool);
    owner._svsmPoolNeedsClear = false;
}

void ShadowRenderer::SVSMUpdateRecorder::PrepareFrameStateAndClipmapAnchors()
{
    // Detect global invalidation, snap clipmap windows and reset frame statistics/list headers.
    commandList.BeginPipeline(owner._svsmPreparePipeline);
    commandList.PushConstant(constants, 0, sizeof(Constants));
    commandList.BindDescriptorSet(data.prepareSet, frameIndex);
    commandList.Dispatch(1, 1, 1);
    commandList.EndPipeline(owner._svsmPreparePipeline);

    commandList.BufferBarrier(data.svsmDataBuffer, Renderer::BufferPassUsage::COMPUTE);
    // Clear-list header resets must be visible to the later atomic appenders. Barriers are
    // per-buffer, so publishing svsmData alone does not cover either list.
    commandList.BufferBarrier(data.clearListBuffer, Renderer::BufferPassUsage::COMPUTE);
    commandList.BufferBarrier(data.dynamicClearListBuffer, Renderer::BufferPassUsage::COMPUTE);
}

void ShadowRenderer::SVSMUpdateRecorder::InvalidatePagesTouchedByChangedCasters()
{
    if (numDirtyAABBs == 0)
        return;

    commandList.BeginPipeline(owner._svsmInvalidateAABBsPipeline);
    commandList.PushConstant(constants, 0, sizeof(Constants));
    commandList.BindDescriptorSet(data.invalidateSet, frameIndex);
    commandList.Dispatch((numDirtyAABBs + 63) / 64, numClipmaps, 1);
    commandList.EndPipeline(owner._svsmInvalidateAABBsPipeline);

    commandList.BufferBarrier(data.pageTableBuffer, Renderer::BufferPassUsage::COMPUTE);
    commandList.BufferBarrier(data.svsmDataBuffer, Renderer::BufferPassUsage::COMPUTE);
}

void ShadowRenderer::SVSMUpdateRecorder::RecycleStaleStaticPages()
{
    // Apply toroidal/global invalidation, age entries and return stale physical pages. The full
    // capacity is visited so entries orphaned by configuration changes also age out.
    commandList.BeginPipeline(owner._svsmPageUpdateAPipeline);
    commandList.PushConstant(constants, 0, sizeof(Constants));
    commandList.BindDescriptorSet(data.updateASet, frameIndex);
    commandList.Dispatch(tableCapacity / 256, 1, 1);
    commandList.EndPipeline(owner._svsmPageUpdateAPipeline);

    commandList.BufferBarrier(data.pageTableBuffer, Renderer::BufferPassUsage::COMPUTE);
    commandList.BufferBarrier(data.freeListBuffer, Renderer::BufferPassUsage::COMPUTE);
}

void ShadowRenderer::SVSMUpdateRecorder::MarkPagesNeededByVisibleReceivers()
{
    commandList.BeginPipeline(owner._svsmPageMarkPipeline);
    commandList.PushConstant(constants, 0, sizeof(Constants));
    data.markSet.Bind("_depth"_h, data.depth);
    commandList.BindDescriptorSet(data.markSet, frameIndex);

    const uvec2 depthDimensions = graphResources.GetImageDimensions(data.depth);
    commandList.Dispatch((depthDimensions.x + 15) / 16, (depthDimensions.y + 15) / 16, 1);
    commandList.EndPipeline(owner._svsmPageMarkPipeline);

    commandList.BufferBarrier(data.pageTableBuffer, Renderer::BufferPassUsage::COMPUTE);
}

void ShadowRenderer::SVSMUpdateRecorder::AllocateAndQueueStaticPages()
{
    // Allocate finest-to-coarsest so pool starvation lands on the coarsest fallback rings. This
    // also re-dirties invalid pages and builds the dirty rectangles and physical clear list.
    commandList.BeginPipeline(owner._svsmPageUpdateBPipeline);
    commandList.BindDescriptorSet(data.updateBSet, frameIndex);

    for (u32 clipmapIndex = 0; clipmapIndex < numClipmaps; clipmapIndex++)
    {
        Constants* allocConstants = graphResources.FrameNew<Constants>();
        *allocConstants = *constants;
        allocConstants->allocClipmap = clipmapIndex;

        commandList.PushConstant(allocConstants, 0, sizeof(Constants));
        commandList.Dispatch((config.pageTableSize * config.pageTableSize) / 256, 1, 1);

        if (clipmapIndex + 1 < numClipmaps)
        {
            commandList.BufferBarrier(data.freeListBuffer, Renderer::BufferPassUsage::COMPUTE);
        }
    }

    commandList.EndPipeline(owner._svsmPageUpdateBPipeline);

    commandList.BufferBarrier(data.pageTableBuffer, Renderer::BufferPassUsage::COMPUTE);
    commandList.BufferBarrier(data.svsmDataBuffer, Renderer::BufferPassUsage::COMPUTE);
    commandList.BufferBarrier(data.clearListBuffer, Renderer::BufferPassUsage::COMPUTE);
}

void ShadowRenderer::SVSMUpdateRecorder::MarkPagesTouchedByDynamicCasters()
{
    if (numDynamicAABBs == 0)
        return;

    Constants* dynamicMarkConstants = graphResources.FrameNew<Constants>();
    *dynamicMarkConstants = *constants;
    dynamicMarkConstants->numDirtyAABBs = numDynamicAABBs;

    commandList.BeginPipeline(owner._svsmDynamicMarkPipeline);
    commandList.PushConstant(dynamicMarkConstants, 0, sizeof(Constants));
    commandList.BindDescriptorSet(data.dynamicMarkSet, frameIndex);

    commandList.Dispatch((numDynamicAABBs + 63) / 64, numClipmaps, 1);
    commandList.EndPipeline(owner._svsmDynamicMarkPipeline);

    commandList.BufferBarrier(data.dynamicPageTableBuffer, Renderer::BufferPassUsage::COMPUTE);
}

void ShadowRenderer::SVSMUpdateRecorder::RebuildDynamicPageSet()
{
    MarkPagesTouchedByDynamicCasters();

    // Release before acquire. Combining free-list pushes and pops in one dispatch can expose a
    // slot before its physical page index is written, aliasing one page under two table entries.
    commandList.BeginPipeline(owner._svsmDynamicUpdatePipeline);
    commandList.PushConstant(constants, 0, sizeof(Constants));
    commandList.BindDescriptorSet(data.dynamicUpdateSet, frameIndex);
    commandList.Dispatch(tableCapacity / 256, 1, 1);

    commandList.BufferBarrier(data.dynamicPageTableBuffer, Renderer::BufferPassUsage::COMPUTE);
    commandList.BufferBarrier(data.dynamicFreeListBuffer, Renderer::BufferPassUsage::COMPUTE);

    Constants* dynamicAcquireConstants = graphResources.FrameNew<Constants>();
    *dynamicAcquireConstants = *constants;
    dynamicAcquireConstants->dynamicPhase = 1;

    commandList.PushConstant(dynamicAcquireConstants, 0, sizeof(Constants));
    commandList.Dispatch(tableCapacity / 256, 1, 1);
    commandList.EndPipeline(owner._svsmDynamicUpdatePipeline);

    commandList.BufferBarrier(data.dynamicPageTableBuffer, Renderer::BufferPassUsage::COMPUTE);
    commandList.BufferBarrier(data.dynamicFreeListBuffer, Renderer::BufferPassUsage::COMPUTE);
    commandList.BufferBarrier(data.dynamicClearListBuffer, Renderer::BufferPassUsage::COMPUTE);
    commandList.BufferBarrier(data.svsmDataBuffer, Renderer::BufferPassUsage::COMPUTE);
}

void ShadowRenderer::SVSMUpdateRecorder::BuildClipmapCamerasAndGeometryDispatches()
{
    commandList.BeginPipeline(owner._svsmFinalizePipeline);
    commandList.PushConstant(constants, 0, sizeof(Constants));
    commandList.BindDescriptorSet(data.finalizeSet, frameIndex);

    commandList.Dispatch(1, 1, 1);
    commandList.EndPipeline(owner._svsmFinalizePipeline);

    commandList.BufferBarrier(data.cameras, Renderer::BufferPassUsage::COMPUTE);
}

void ShadowRenderer::SVSMUpdateRecorder::ClearPagesQueuedForRendering()
{
    commandList.BeginPipeline(owner._svsmPageClearPipeline);
    commandList.PushConstant(constants, 0, sizeof(Constants));

    data.clearSet.Bind("_pagePool"_h, data.pagePool);
    commandList.BindDescriptorSet(data.clearSet, frameIndex);

    commandList.DispatchIndirect(data.clearListBuffer, 0);
    commandList.EndPipeline(owner._svsmPageClearPipeline);

    if (!dynamicSplit)
        return;

    Constants* dynamicClearConstants = graphResources.FrameNew<Constants>();
    *dynamicClearConstants = *constants;
    dynamicClearConstants->poolPagesPerRow = constants->dynamicPoolPagesPerRow;

    commandList.BeginPipeline(owner._svsmPageClearPipeline);
    commandList.PushConstant(dynamicClearConstants, 0, sizeof(Constants));

    data.dynamicClearSet.Bind("_pagePool"_h, data.dynamicPagePool);
    commandList.BindDescriptorSet(data.dynamicClearSet, frameIndex);

    commandList.DispatchIndirect(data.dynamicClearListBuffer, 0);
    commandList.EndPipeline(owner._svsmPageClearPipeline);
}

void ShadowRenderer::SVSMUpdateRecorder::CaptureDiagnostics()
{
    commandList.CopyBuffer(data.svsmDataReadBackBuffer, 0, data.svsmDataBuffer, 0, sizeof(u32) * SVSM_DATA_UINT_COUNT);

    if (!dynamicSplit || owner._svsmValidatePending || CVAR_SVSMValidateDynamic.Get() == 0)
        return;

    const u32 tableUints = SVSM_MAX_CLIPMAPS * SVSM_MAX_PAGE_TABLE_SIZE * SVSM_MAX_PAGE_TABLE_SIZE;
    commandList.CopyBuffer(data.dynamicValidateReadBackBuffer, 0, data.dynamicPageTableBuffer, 0, sizeof(u32) * tableUints);
    commandList.CopyBuffer(data.dynamicValidateReadBackBuffer, sizeof(u32) * tableUints, data.dynamicFreeListBuffer, 0, sizeof(u32) * (4 + SVSM_MAX_POOL_PAGES));
    owner._svsmValidatePending = true;
}

void ShadowRenderer::AddSVSMUpdatePass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex)
{
    const u32 numClipmaps = static_cast<u32>(glm::clamp(CVAR_SVSMNumClipmaps.Get(), 1, static_cast<i32>(SVSM_MAX_CLIPMAPS)));
    const bool enabled = CVAR_ShadowEnabled.Get() && !CVAR_SVSMFreeze.Get() && !_svsmNightActive;
    if (!enabled || _svsmPagePool == Renderer::ImageID::Invalid())
        return;

    // Record-time inputs, the shadow sun steps in discrete intervals so cached pages stay valid
    // between steps
    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
    auto& dayNightCycle = registry->ctx().get<ECS::Singletons::DayNightCycle>();
    const f32 shadowTimeOfDay = ECS::Systems::GetShadowTimeOfDay(dayNightCycle.GetTimeInSecondsF32());
    // GetLightDirection points toward the sun (the shading convention); SVSM works with the
    // direction the light travels
    const vec3 lightDirection = -ECS::Systems::UpdateAreaLights::GetLightDirection(shadowTimeOfDay);

    u32 invalidateCause = 0;
    if (CVAR_SVSMInvalidateAll.Get() != 0 || _svsmForceInvalidateAll)
    {
        invalidateCause |= SVSMCause::Manual;
        CVAR_SVSMInvalidateAll.Set(0);
        _svsmForceInvalidateAll = false;
    }
    if (_svsmDirtyAABBOverflow)
    {
        invalidateCause |= SVSMCause::AABBOverflow;
    }

    const u32 numDirtyAABBs = static_cast<u32>(_svsmDirtyAABBs.size() / 2);
    const bool dynamicSplit = CVAR_SVSMDynamicSplit.Get() == 1;
    const u32 numDynamicAABBs = dynamicSplit ? static_cast<u32>(_svsmDynamicAABBs.size() / 2) : 0;

    renderGraph->AddPass<SVSMUpdatePassData>("SVSM Update",
        [this, &resources](SVSMUpdatePassData& data, Renderer::RenderGraphBuilder& builder)
        {
            return data.DeclareResources(*this, resources, builder);
        },
        [this, frameIndex, numClipmaps, lightDirection, invalidateCause, numDirtyAABBs, dynamicSplit, numDynamicAABBs](SVSMUpdatePassData& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList)
        {
            SVSMUpdateRecorder recorder(*this, data, graphResources, commandList, frameIndex, numClipmaps,
                lightDirection, invalidateCause, numDirtyAABBs, dynamicSplit, numDynamicAABBs);
            recorder.Record();
        });
}

void ShadowRenderer::AddSVSMDebugOverlayPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex)
{
    struct Data
    {
        Renderer::ImageMutableResource target;
        Renderer::ImageResource pagePool;

        Renderer::DescriptorSetResource debugSet;
        Renderer::DescriptorSetResource poolDebugSet;
    };

    const i32 debugClipmap = CVAR_SVSMDebugClipmap.Get();
    const i32 poolMode = CVAR_SVSMDebugShowPool.Get(); // 1 = static pool, 2 = dynamic pool
    const bool showPool = poolMode != 0 && _svsmPagePool != Renderer::ImageID::Invalid();
    const u32 numClipmaps = static_cast<u32>(glm::clamp(CVAR_SVSMNumClipmaps.Get(), 1, static_cast<i32>(SVSM_MAX_CLIPMAPS)));
    if (!CVAR_ShadowEnabled.Get() || (debugClipmap < 0 && !showPool))
        return;

    renderGraph->AddPass<Data>("SVSM Debug Overlay",
        [this, &resources, showPool, poolMode](Data& data, Renderer::RenderGraphBuilder& builder)
        {
            data.target = builder.Write(resources.sceneColor, Renderer::PipelineType::COMPUTE, Renderer::LoadMode::LOAD);
            builder.Read(_svsmPageTableBuffer, Renderer::BufferPassUsage::COMPUTE);
            if (showPool)
            {
                data.pagePool = builder.Read(poolMode == 2 ? _svsmDynamicPagePool : _svsmPagePool, Renderer::PipelineType::COMPUTE);
            }

            data.debugSet = builder.Use(_svsmPageTableDebugDescriptorSet);
            data.poolDebugSet = builder.Use(_svsmPoolDebugDescriptorSet);

            return true; // Return true from setup to enable this pass, return false to disable it
        },
        [this, frameIndex, debugClipmap, numClipmaps, showPool, poolMode](Data& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList)
        {
            GPU_SCOPED_PROFILER_ZONE(commandList, SVSMDebugOverlay);

            uvec2 targetDimensions = graphResources.GetImageDimensions(data.target);

            if (debugClipmap >= 0)
            {
                struct DebugConstants
                {
                    u32 clipmapIndex;
                    u32 pageTableSize;
                    u32 cellSize;
                    u32 padding0;
                    ivec2 screenOffset;
                };

                const u32 pageTableSize = DeriveSVSMConfig(SVSM_MAX_PAGE_TABLE_SIZE).pageTableSize;
                const u32 cellSize = 8;
                const u32 regionSize = pageTableSize * cellSize;

                DebugConstants* constants = graphResources.FrameNew<DebugConstants>();
                constants->clipmapIndex = glm::min(static_cast<u32>(debugClipmap), numClipmaps - 1);
                constants->pageTableSize = pageTableSize;
                constants->cellSize = cellSize;
                constants->screenOffset = ivec2(glm::max(static_cast<i32>(targetDimensions.x) - static_cast<i32>(regionSize) - 8, 0), 8);

                commandList.BeginPipeline(_svsmPageTableDebugPipeline);
                commandList.PushConstant(constants, 0, sizeof(DebugConstants));

                data.debugSet.Bind("_target"_h, data.target);
                commandList.BindDescriptorSet(data.debugSet, frameIndex);

                commandList.Dispatch((regionSize + 15) / 16, (regionSize + 15) / 16, 1);
                commandList.EndPipeline(_svsmPageTableDebugPipeline);
            }

            if (showPool)
            {
                struct PoolDebugConstants
                {
                    u32 poolSize;
                    u32 regionSize;
                    u32 padding0;
                    u32 padding1;
                    ivec2 screenOffset;
                };

                const u32 regionSize = 512;

                PoolDebugConstants* constants = graphResources.FrameNew<PoolDebugConstants>();
                constants->poolSize = static_cast<u32>(poolMode == 2 ? CVAR_SVSMDynamicPoolSize.Get() : CVAR_SVSMPoolSize.Get());
                constants->regionSize = regionSize;
                constants->screenOffset = ivec2(glm::max(static_cast<i32>(targetDimensions.x) - static_cast<i32>(regionSize) - 8, 0), glm::max(static_cast<i32>(targetDimensions.y) - static_cast<i32>(regionSize) - 8, 0));

                commandList.BeginPipeline(_svsmPoolDebugPipeline);
                commandList.PushConstant(constants, 0, sizeof(PoolDebugConstants));

                data.poolDebugSet.Bind("_pagePool"_h, data.pagePool);
                data.poolDebugSet.Bind("_target"_h, data.target);
                commandList.BindDescriptorSet(data.poolDebugSet, frameIndex);

                commandList.Dispatch((regionSize + 15) / 16, (regionSize + 15) / 16, 1);
                commandList.EndPipeline(_svsmPoolDebugPipeline);
            }
        });
}

bool ShadowRenderer::GetSVSMClipmapStats(u32 clipmapIndex, SVSMClipmapStats& outStats) const
{
    if (clipmapIndex >= SVSM_MAX_CLIPMAPS)
        return false;

    outStats.marked = _svsmDataReadBack[SVSMDataOffsets::StatsMarked + clipmapIndex];
    outStats.resident = _svsmDataReadBack[SVSMDataOffsets::StatsResident + clipmapIndex];
    outStats.dirty = _svsmDataReadBack[SVSMDataOffsets::StatsDirty + clipmapIndex];
    outStats.evicted = _svsmDataReadBack[SVSMDataOffsets::StatsEvicted + clipmapIndex];
    outStats.invalidated = _svsmDataReadBack[SVSMDataOffsets::StatsInvalidated + clipmapIndex];
    outStats.dynamicLive = _svsmDataReadBack[SVSMDataOffsets::StatsDynamicLive + clipmapIndex];
    outStats.deferred = _svsmDataReadBack[SVSMDataOffsets::StatsDeferred + clipmapIndex];
    outStats.extent = glm::uintBitsToFloat(_svsmDataReadBack[SVSMDataOffsets::Extent + clipmapIndex]);
    return true;
}

void ShadowRenderer::GetSVSMGlobalStats(u32& outFreePages, u32& outTotalPages, u32& outOverflow, u32& outInvalidationCause) const
{
    outFreePages = _svsmDataReadBack[SVSMDataOffsets::StatsFreeListCount];
    outTotalPages = _svsmPoolPages;
    outOverflow = _svsmDataReadBack[SVSMDataOffsets::StatsOverflow];
    outInvalidationCause = _svsmDataReadBack[SVSMDataOffsets::StatsInvalidationCause];
}

void ShadowRenderer::GetSVSMDynamicStats(u32& outLivePages, u32& outTotalPages, u32& outOverflow) const
{
    outLivePages = _svsmDataReadBack[SVSMDataOffsets::StatsDynamicTotal];
    outTotalPages = _svsmDynamicPoolPages;
    outOverflow = _svsmDataReadBack[SVSMDataOffsets::StatsDynamicOverflow];
}

u32 ShadowRenderer::GetSVSMBudgetUsed() const
{
    return _svsmDataReadBack[SVSMDataOffsets::StatsBudgetUsed];
}

void ShadowRenderer::AddSVSMBindPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex)
{
    struct Data
    {
        Renderer::ImageResource svsmPagePool;
        Renderer::ImageResource svsmDynamicPagePool;

        Renderer::DescriptorSetResource lightDescriptorSet;
    };

    // Runs unconditionally (even under svsmFreeze, when the update pass early-outs): every LIGHT
    // set consumer needs the pool bindings valid, real pools or the placeholder before they exist
    renderGraph->AddPass<Data>("SVSM Bind",
        [this, &resources](Data& data, Renderer::RenderGraphBuilder& builder)
        {
            data.svsmPagePool = builder.Read(GetSVSMPagePoolOrPlaceholder(), Renderer::PipelineType::COMPUTE);
            data.svsmDynamicPagePool = builder.Read(GetSVSMDynamicPagePoolOrPlaceholder(), Renderer::PipelineType::COMPUTE);

            data.lightDescriptorSet = builder.Use(resources.lightDescriptorSet);

            return true; // Return true from setup to enable this pass, return false to disable it
        },
        [](Data& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList)
        {
            data.lightDescriptorSet.Bind("_svsmPagePool"_h, data.svsmPagePool);
            data.lightDescriptorSet.Bind("_svsmDynamicPagePool"_h, data.svsmDynamicPagePool);
        });
}

void ShadowRenderer::CreatePermanentResources(RenderResources& resources)
{
    ZoneScoped;

    SanitizeSVSMConfigCVars(SVSM_MAX_PAGE_TABLE_SIZE);

    // SVSM page table lifecycle
    {
        Renderer::ComputePipelineDesc pipelineDesc;
        Renderer::ComputeShaderDesc shaderDesc;

        pipelineDesc.debugName = "SVSM Prepare";
        shaderDesc.shaderEntry = _gameRenderer->GetShaderEntry("Shadows/SVSMPrepare.cs"_h, "Shadows/SVSMPrepare.cs");
        pipelineDesc.computeShader = _renderer->LoadShader(shaderDesc);
        _svsmPreparePipeline = _renderer->CreatePipeline(pipelineDesc);
        _svsmPrepareDescriptorSet.RegisterPipeline(_renderer, _svsmPreparePipeline);
        _svsmPrepareDescriptorSet.Init(_renderer);

        pipelineDesc.debugName = "SVSM Invalidate AABBs";
        shaderDesc.shaderEntry = _gameRenderer->GetShaderEntry("Shadows/SVSMInvalidateAABBs.cs"_h, "Shadows/SVSMInvalidateAABBs.cs");
        pipelineDesc.computeShader = _renderer->LoadShader(shaderDesc);
        _svsmInvalidateAABBsPipeline = _renderer->CreatePipeline(pipelineDesc);
        _svsmInvalidateAABBsDescriptorSet.RegisterPipeline(_renderer, _svsmInvalidateAABBsPipeline);
        _svsmInvalidateAABBsDescriptorSet.Init(_renderer);

        pipelineDesc.debugName = "SVSM Page Update A";
        shaderDesc.shaderEntry = _gameRenderer->GetShaderEntry("Shadows/SVSMPageUpdateA.cs"_h, "Shadows/SVSMPageUpdateA.cs");
        pipelineDesc.computeShader = _renderer->LoadShader(shaderDesc);
        _svsmPageUpdateAPipeline = _renderer->CreatePipeline(pipelineDesc);
        _svsmPageUpdateADescriptorSet.RegisterPipeline(_renderer, _svsmPageUpdateAPipeline);
        _svsmPageUpdateADescriptorSet.Init(_renderer);

        pipelineDesc.debugName = "SVSM Page Mark";
        shaderDesc.shaderEntry = _gameRenderer->GetShaderEntry("Shadows/SVSMPageMark.cs"_h, "Shadows/SVSMPageMark.cs");
        pipelineDesc.computeShader = _renderer->LoadShader(shaderDesc);
        _svsmPageMarkPipeline = _renderer->CreatePipeline(pipelineDesc);
        _svsmPageMarkDescriptorSet.RegisterPipeline(_renderer, _svsmPageMarkPipeline);
        _svsmPageMarkDescriptorSet.Init(_renderer);

        pipelineDesc.debugName = "SVSM Page Update B";
        shaderDesc.shaderEntry = _gameRenderer->GetShaderEntry("Shadows/SVSMPageUpdateB.cs"_h, "Shadows/SVSMPageUpdateB.cs");
        pipelineDesc.computeShader = _renderer->LoadShader(shaderDesc);
        _svsmPageUpdateBPipeline = _renderer->CreatePipeline(pipelineDesc);
        _svsmPageUpdateBDescriptorSet.RegisterPipeline(_renderer, _svsmPageUpdateBPipeline);
        _svsmPageUpdateBDescriptorSet.Init(_renderer);

        pipelineDesc.debugName = "SVSM Dynamic Mark";
        shaderDesc.shaderEntry = _gameRenderer->GetShaderEntry("Shadows/SVSMDynamicMark.cs"_h, "Shadows/SVSMDynamicMark.cs");
        pipelineDesc.computeShader = _renderer->LoadShader(shaderDesc);
        _svsmDynamicMarkPipeline = _renderer->CreatePipeline(pipelineDesc);
        _svsmDynamicMarkDescriptorSet.RegisterPipeline(_renderer, _svsmDynamicMarkPipeline);
        _svsmDynamicMarkDescriptorSet.Init(_renderer);

        pipelineDesc.debugName = "SVSM Dynamic Update";
        shaderDesc.shaderEntry = _gameRenderer->GetShaderEntry("Shadows/SVSMDynamicUpdate.cs"_h, "Shadows/SVSMDynamicUpdate.cs");
        pipelineDesc.computeShader = _renderer->LoadShader(shaderDesc);
        _svsmDynamicUpdatePipeline = _renderer->CreatePipeline(pipelineDesc);
        _svsmDynamicUpdateDescriptorSet.RegisterPipeline(_renderer, _svsmDynamicUpdatePipeline);
        _svsmDynamicUpdateDescriptorSet.Init(_renderer);

        pipelineDesc.debugName = "SVSM Finalize";
        shaderDesc.shaderEntry = _gameRenderer->GetShaderEntry("Shadows/SVSMFinalize.cs"_h, "Shadows/SVSMFinalize.cs");
        pipelineDesc.computeShader = _renderer->LoadShader(shaderDesc);
        _svsmFinalizePipeline = _renderer->CreatePipeline(pipelineDesc);
        _svsmFinalizeDescriptorSet.RegisterPipeline(_renderer, _svsmFinalizePipeline);
        _svsmFinalizeDescriptorSet.Init(_renderer);

        pipelineDesc.debugName = "SVSM Page Clear";
        shaderDesc.shaderEntry = _gameRenderer->GetShaderEntry("Shadows/SVSMPageClear.cs"_h, "Shadows/SVSMPageClear.cs");
        pipelineDesc.computeShader = _renderer->LoadShader(shaderDesc);
        _svsmPageClearPipeline = _renderer->CreatePipeline(pipelineDesc);
        _svsmPageClearDescriptorSet.RegisterPipeline(_renderer, _svsmPageClearPipeline);
        _svsmPageClearDescriptorSet.Init(_renderer);

        // Second set instance on the same clear pipeline, bound to the dynamic list + pool
        _svsmDynamicPageClearDescriptorSet.RegisterPipeline(_renderer, _svsmPageClearPipeline);
        _svsmDynamicPageClearDescriptorSet.Init(_renderer);

        pipelineDesc.debugName = "SVSM Page Table Debug";
        shaderDesc.shaderEntry = _gameRenderer->GetShaderEntry("Shadows/SVSMPageTableDebug.cs"_h, "Shadows/SVSMPageTableDebug.cs");
        pipelineDesc.computeShader = _renderer->LoadShader(shaderDesc);
        _svsmPageTableDebugPipeline = _renderer->CreatePipeline(pipelineDesc);
        _svsmPageTableDebugDescriptorSet.RegisterPipeline(_renderer, _svsmPageTableDebugPipeline);
        _svsmPageTableDebugDescriptorSet.Init(_renderer);

        pipelineDesc.debugName = "SVSM Pool Debug";
        shaderDesc.shaderEntry = _gameRenderer->GetShaderEntry("Shadows/SVSMPoolDebug.cs"_h, "Shadows/SVSMPoolDebug.cs");
        pipelineDesc.computeShader = _renderer->LoadShader(shaderDesc);
        _svsmPoolDebugPipeline = _renderer->CreatePipeline(pipelineDesc);
        _svsmPoolDebugDescriptorSet.RegisterPipeline(_renderer, _svsmPoolDebugPipeline);
        _svsmPoolDebugDescriptorSet.Init(_renderer);

        Renderer::BufferDesc bufferDesc;
        bufferDesc.name = "SVSMDirtyAABBs";
        bufferDesc.size = sizeof(vec4) * SVSM_MAX_DIRTY_AABBS * 2;
        bufferDesc.usage = Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION;
        _svsmDirtyAABBBuffer = _renderer->CreateBuffer(_svsmDirtyAABBBuffer, bufferDesc);

        bufferDesc.name = "SVSMDynamicAABBs";
        bufferDesc.size = sizeof(vec4) * SVSM_MAX_DYNAMIC_AABBS * 2;
        _svsmDynamicAABBBuffer = _renderer->CreateBuffer(_svsmDynamicAABBBuffer, bufferDesc);

        bufferDesc.name = "SVSMDynamicClearList";
        bufferDesc.size = sizeof(u32) * (4 + SVSM_MAX_POOL_PAGES);
        bufferDesc.usage = Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::INDIRECT_ARGUMENT_BUFFER;
        _svsmDynamicClearListBuffer = _renderer->CreateAndFillBuffer(_svsmDynamicClearListBuffer, bufferDesc, [](void* mappedMemory, size_t size)
        {
            u32* values = static_cast<u32*>(mappedMemory);
            memset(values, 0, size);
            values[1] = 1; // Indirect group counts y and z
            values[2] = 1;
        });

        bufferDesc.name = "SVSMClearList";
        bufferDesc.size = sizeof(u32) * (4 + SVSM_MAX_POOL_PAGES); // [0..2] indirect dispatch args, physical pages from [4]
        bufferDesc.usage = Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::INDIRECT_ARGUMENT_BUFFER;
        _svsmClearListBuffer = _renderer->CreateAndFillBuffer(_svsmClearListBuffer, bufferDesc, [](void* mappedMemory, size_t size)
        {
            u32* values = static_cast<u32*>(mappedMemory);
            memset(values, 0, size);
            values[1] = 1; // Indirect group counts y and z
            values[2] = 1;
        });

        // The config-independent buffer binds. The config-shaped buffers (SVSMData, page tables,
        // free lists) are created and bound by ResetSVSMPoolState below
        _svsmPrepareDescriptorSet.Bind("_clearList"_h, _svsmClearListBuffer);
        _svsmPrepareDescriptorSet.Bind("_dynamicClearList"_h, _svsmDynamicClearListBuffer);
        _svsmInvalidateAABBsDescriptorSet.Bind("_dirtyAABBs"_h, _svsmDirtyAABBBuffer);
        _svsmPageUpdateBDescriptorSet.Bind("_clearList"_h, _svsmClearListBuffer);
        _svsmDynamicMarkDescriptorSet.Bind("_dynamicAABBs"_h, _svsmDynamicAABBBuffer);
        _svsmDynamicUpdateDescriptorSet.Bind("_dynamicClearList"_h, _svsmDynamicClearListBuffer);
        _svsmPageClearDescriptorSet.Bind("_clearList"_h, _svsmClearListBuffer);
        _svsmDynamicPageClearDescriptorSet.Bind("_clearList"_h, _svsmDynamicClearListBuffer);
        // _srcCameras / _depth / _target / _rwCameras / _pagePool are bound per frame, those resources can be recreated

        // Per-view fill dispatch args written by Finalize, consumed by the geometry passes'
        // DispatchIndirect: per clipmap 5 x uvec3 (model static, model dynamic, terrain static,
        // model static overhead, model dynamic overhead). Prefilled with zero groups so a
        // pre-Finalize consumer launches nothing
        Renderer::BufferDesc fillArgsDesc;
        fillArgsDesc.name = "SVSMFillDispatchArgs";
        fillArgsDesc.size = SVSM_FILL_ARGS_VIEW_STRIDE * SVSM_MAX_CLIPMAPS;
        fillArgsDesc.usage = Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::INDIRECT_ARGUMENT_BUFFER;
        _svsmFillArgsBuffer = _renderer->CreateAndFillBuffer(_svsmFillArgsBuffer, fillArgsDesc, [](void* mappedMemory, size_t size)
        {
            u32* values = static_cast<u32*>(mappedMemory);
            for (u32 i = 0; i < SVSM_MAX_CLIPMAPS * 15; i += 3)
            {
                values[i + 0] = 0;
                values[i + 1] = 1;
                values[i + 2] = 1;
            }
        });
        _svsmFinalizeDescriptorSet.Bind("_fillDispatchArgs"_h, _svsmFillArgsBuffer);

        bufferDesc.name = "SVSMDataReadBack";
        bufferDesc.size = sizeof(u32) * SVSM_DATA_UINT_COUNT;
        bufferDesc.usage = Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION;
        bufferDesc.cpuAccess = Renderer::BufferCPUAccess::ReadOnly;
        _svsmDataReadBackBuffer = _renderer->CreateBuffer(_svsmDataReadBackBuffer, bufferDesc);

        bufferDesc.name = "SVSMDynamicValidateReadBack";
        bufferDesc.size = sizeof(u32) * (SVSM_MAX_CLIPMAPS * SVSM_MAX_PAGE_TABLE_SIZE * SVSM_MAX_PAGE_TABLE_SIZE + 4 + SVSM_MAX_POOL_PAGES);
        _svsmDynamicValidateReadBackBuffer = _renderer->CreateBuffer(_svsmDynamicValidateReadBackBuffer, bufferDesc);

        Renderer::ImageDesc placeholderDesc;
        placeholderDesc.debugName = "SVSMPagePoolPlaceholder";
        placeholderDesc.dimensions = vec2(1, 1);
        placeholderDesc.dimensionType = Renderer::ImageDimensionType::DIMENSION_ABSOLUTE;
        placeholderDesc.format = Renderer::ImageFormat::R32_UINT;
        placeholderDesc.sampleCount = Renderer::SampleCount::SAMPLE_COUNT_1;
        placeholderDesc.clearUInts = uvec4(0, 0, 0, 0);

        _svsmPagePoolPlaceholder = _renderer->CreateImage(placeholderDesc);

        // Creates the config-shaped buffers and binds them everywhere, here and again on live
        // config edits
        ResetSVSMPoolState(resources);
    }

    BindCameraBuffers(resources);
}

void ShadowRenderer::ResetSVSMPoolState(RenderResources& resources)
{
    // (Re)shapes everything svsmPageSize touches: pool page counts, free lists, page tables and
    // the SVSMData scratch (zeroed SVSMData reads as uninitialized, Prepare re-derives the
    // anchors). The refills happen CPU-side so GPU eviction never runs against a half-rebuilt
    // free list, and CreateAndFillBuffer swaps in a fresh buffer (deferred-destroying the old one
    // past the frames in flight), so every consumer set rebinds below. The pool textures are NOT
    // recreated — their dimensions only depend on the pool size cvars, which are restart-only
    // once the pools exist (the Engine cannot destroy images)
    const SVSMDerivedConfig config = DeriveSVSMConfig(SVSM_MAX_PAGE_TABLE_SIZE);
    _svsmAppliedPageSize = config.pageSize;
    _svsmAppliedPoolSize = static_cast<u32>(CVAR_SVSMPoolSize.Get());
    _svsmAppliedDynamicPoolSize = static_cast<u32>(CVAR_SVSMDynamicPoolSize.Get());

    // The physical page index is 12 bits in the table entry
    _svsmPoolPages = glm::min(config.poolPagesPerRow * config.poolPagesPerRow, SVSM_MAX_POOL_PAGES);
    _svsmDynamicPoolPages = glm::min(config.dynamicPoolPagesPerRow * config.dynamicPoolPagesPerRow, SVSM_MAX_POOL_PAGES);

    Renderer::BufferDesc bufferDesc;
    bufferDesc.name = "SVSMData";
    bufferDesc.size = sizeof(u32) * SVSM_DATA_UINT_COUNT;
    bufferDesc.usage = Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION | Renderer::BufferUsage::TRANSFER_SOURCE;
    _svsmDataBuffer = _renderer->CreateAndFillBuffer(_svsmDataBuffer, bufferDesc, [](void* mappedMemory, size_t size)
    {
        memset(mappedMemory, 0, size); // prevLightDirection.w = 0 marks the state uninitialized
    });

    bufferDesc.name = "SVSMPageTable";
    bufferDesc.size = sizeof(u32) * SVSM_MAX_CLIPMAPS * SVSM_MAX_PAGE_TABLE_SIZE * SVSM_MAX_PAGE_TABLE_SIZE;
    bufferDesc.usage = Renderer::BufferUsage::STORAGE_BUFFER;
    _svsmPageTableBuffer = _renderer->CreateAndFillBuffer(_svsmPageTableBuffer, bufferDesc, [](void* mappedMemory, size_t size)
    {
        memset(mappedMemory, 0, size); // A zeroed entry is a free slot
    });

    bufferDesc.name = "SVSMFreeList";
    bufferDesc.size = sizeof(u32) * (4 + SVSM_MAX_POOL_PAGES);
    bufferDesc.usage = Renderer::BufferUsage::STORAGE_BUFFER;
    const u32 poolPages = _svsmPoolPages;
    _svsmFreeListBuffer = _renderer->CreateAndFillBuffer(_svsmFreeListBuffer, bufferDesc, [poolPages](void* mappedMemory, size_t size)
    {
        u32* values = static_cast<u32*>(mappedMemory);
        values[0] = poolPages; // [0] = count, [1..3] padding, entries from [4]
        values[1] = 0;
        values[2] = 0;
        values[3] = 0;
        for (u32 i = 0; i < SVSM_MAX_POOL_PAGES; i++)
        {
            values[4 + i] = i < poolPages ? i : 0;
        }
    });

    bufferDesc.name = "SVSMDynamicPageTable";
    bufferDesc.size = sizeof(u32) * SVSM_MAX_CLIPMAPS * SVSM_MAX_PAGE_TABLE_SIZE * SVSM_MAX_PAGE_TABLE_SIZE;
    bufferDesc.usage = Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_SOURCE; // Source of the svsmValidateDynamic snapshot copy
    _svsmDynamicPageTableBuffer = _renderer->CreateAndFillBuffer(_svsmDynamicPageTableBuffer, bufferDesc, [](void* mappedMemory, size_t size)
    {
        memset(mappedMemory, 0, size);
    });

    bufferDesc.name = "SVSMDynamicFreeList";
    bufferDesc.size = sizeof(u32) * (4 + SVSM_MAX_POOL_PAGES);
    bufferDesc.usage = Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_SOURCE;
    const u32 dynamicPoolPages = _svsmDynamicPoolPages;
    _svsmDynamicFreeListBuffer = _renderer->CreateAndFillBuffer(_svsmDynamicFreeListBuffer, bufferDesc, [dynamicPoolPages](void* mappedMemory, size_t size)
    {
        u32* values = static_cast<u32*>(mappedMemory);
        memset(values, 0, size);
        values[0] = dynamicPoolPages;
        for (u32 i = 0; i < dynamicPoolPages; i++)
        {
            values[4 + i] = i;
        }
    });

    _svsmPrepareDescriptorSet.Bind("_svsmData"_h, _svsmDataBuffer);
    _svsmPrepareDescriptorSet.Bind("_freeList"_h, _svsmFreeListBuffer);
    _svsmInvalidateAABBsDescriptorSet.Bind("_svsmData"_h, _svsmDataBuffer);
    _svsmInvalidateAABBsDescriptorSet.Bind("_pageTable"_h, _svsmPageTableBuffer);
    _svsmPageUpdateADescriptorSet.Bind("_svsmData"_h, _svsmDataBuffer);
    _svsmPageUpdateADescriptorSet.Bind("_pageTable"_h, _svsmPageTableBuffer);
    _svsmPageUpdateADescriptorSet.Bind("_freeList"_h, _svsmFreeListBuffer);
    _svsmPageMarkDescriptorSet.Bind("_svsmData"_h, _svsmDataBuffer);
    _svsmPageMarkDescriptorSet.Bind("_pageTable"_h, _svsmPageTableBuffer);
    _svsmPageUpdateBDescriptorSet.Bind("_svsmData"_h, _svsmDataBuffer);
    _svsmPageUpdateBDescriptorSet.Bind("_pageTable"_h, _svsmPageTableBuffer);
    _svsmPageUpdateBDescriptorSet.Bind("_freeList"_h, _svsmFreeListBuffer);
    _svsmDynamicMarkDescriptorSet.Bind("_svsmData"_h, _svsmDataBuffer);
    _svsmDynamicMarkDescriptorSet.Bind("_pageTable"_h, _svsmPageTableBuffer);
    _svsmDynamicMarkDescriptorSet.Bind("_dynamicPageTable"_h, _svsmDynamicPageTableBuffer);
    _svsmDynamicUpdateDescriptorSet.Bind("_svsmData"_h, _svsmDataBuffer);
    _svsmDynamicUpdateDescriptorSet.Bind("_dynamicPageTable"_h, _svsmDynamicPageTableBuffer);
    _svsmDynamicUpdateDescriptorSet.Bind("_dynamicFreeList"_h, _svsmDynamicFreeListBuffer);
    _svsmFinalizeDescriptorSet.Bind("_svsmData"_h, _svsmDataBuffer);
    _svsmPageTableDebugDescriptorSet.Bind("_pageTable"_h, _svsmPageTableBuffer);

    // The material pass samples SVSM through the LIGHT set. The buffers bind here, the pools
    // bind per frame in the material pass (real pools once created, a placeholder before)
    resources.lightDescriptorSet.Bind("_svsmData"_h, _svsmDataBuffer);
    resources.lightDescriptorSet.Bind("_svsmPageTable"_h, _svsmPageTableBuffer);
    resources.lightDescriptorSet.Bind("_svsmDynamicPageTable"_h, _svsmDynamicPageTableBuffer);

    // The terrain/model SVSM page-render sets read these permanent buffers, bind them once
    // here (the pools stay per-frame, they are created lazily). Same for the cameras buffer
    _terrainRenderer->BindSVSMBuffers(_svsmDataBuffer, _svsmPageTableBuffer);
    _modelRenderer->BindSVSMBuffers(_svsmDataBuffer, _svsmPageTableBuffer, _svsmDynamicPageTableBuffer);
}

void ShadowRenderer::BindCameraBuffers(RenderResources& resources)
{
    Renderer::BufferID camerasBuffer = resources.cameras.GetBuffer();

    _svsmPrepareDescriptorSet.Bind("_srcCameras"_h, camerasBuffer);
    _svsmPageMarkDescriptorSet.Bind("_srcCameras"_h, camerasBuffer);
    _svsmFinalizeDescriptorSet.Bind("_rwCameras"_h, camerasBuffer);
}
