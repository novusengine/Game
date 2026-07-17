#include "ShadowRenderer.h"
#include <Game-Lib/Application/EnttRegistries.h>
#include <Game-Lib/ECS/Components/AABB.h>
#include <Game-Lib/ECS/Components/AnimationData.h>
#include <Game-Lib/ECS/Components/Camera.h>
#include <Game-Lib/ECS/Components/Model.h>
#include <Game-Lib/ECS/Singletons/DayNightCycle.h>
#include <Game-Lib/ECS/Util/Transforms.h>

#include <Game-Lib/ECS/Systems/UpdateAreaLights.h>
#include <Game-Lib/Rendering/Debug/DebugRenderer.h>
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

AutoCVar_Int CVAR_ShadowEnabled(CVarCategory::Client | CVarCategory::Rendering, "shadowEnabled", "enable shadows", 1, CVarFlags::EditCheckbox);
AutoCVar_Float CVAR_ShadowStrength(CVarCategory::Client | CVarCategory::Rendering, "shadowStrength", "directional shadow strength, overwritten each frame from the sun elevation", 1.0f);
AutoCVar_Float CVAR_ShadowNormalOffsetBias(CVarCategory::Client | CVarCategory::Rendering, "shadowNormalOffsetBias", "receiver offset along the surface normal in shadow texels, fights acne on hard angles", 1.0f);
AutoCVar_Float CVAR_ShadowCasterMargin(CVarCategory::Client | CVarCategory::Rendering, "shadowCasterMargin", "extends clipmap culling toward the sun so far-away casters with long shadows are not culled, depth clamp pancakes them onto the near plane", 2500.0f);
AutoCVar_Int CVAR_SVSMNumClipmaps(CVarCategory::Client | CVarCategory::Rendering, "svsmNumClipmaps", "number of SVSM clipmap rings, each ring doubles the covered area", 6);
AutoCVar_Float CVAR_SVSMClipmap0Extent(CVarCategory::Client | CVarCategory::Rendering, "svsmClipmap0Extent", "world extent of the finest SVSM clipmap window in meters, finer rings multiply the near-field page demand", 64.0f);
AutoCVar_Int CVAR_SVSMVirtualSize(CVarCategory::Client | CVarCategory::Rendering, "svsmVirtualSize", "virtual texture resolution per clipmap", 8192);
AutoCVar_Int CVAR_SVSMPageSize(CVarCategory::Client | CVarCategory::Rendering, "svsmPageSize", "texels per page", 128);
AutoCVar_Int CVAR_SVSMPoolSize(CVarCategory::Client | CVarCategory::Rendering, "svsmPoolSize", "physical page pool texture resolution, page count is fixed at startup", 8192);
AutoCVar_Int CVAR_SVSMPageEvictAge(CVarCategory::Client | CVarCategory::Rendering, "svsmPageEvictAge", "frames without a visible sample before a cached page returns to the pool, the age counter caps at 254", 240);
AutoCVar_Float CVAR_SVSMMarkBorderTexels(CVarCategory::Client | CVarCategory::Rendering, "svsmMarkBorderTexels", "filter footprint margin in texels, samples near a page border also mark the neighbor page", 4.0f);
AutoCVar_Float CVAR_SVSMResolutionScale(CVarCategory::Client | CVarCategory::Rendering, "svsmResolutionScale", "eye-distance clipmap floor: skip rings finer than the sample's screen footprint times this, 0 disables, lower = sharper distant shadows for more pages. Below ~0.25 the pool pressure-evicts and churns", 1.0f);
AutoCVar_Int CVAR_SVSMDynamicSplit(CVarCategory::Client | CVarCategory::Rendering, "svsmDynamicSplit", "static/dynamic caster split: animated and moving casters render into a transient dynamic pool instead of churning the static cache, 0 reverts to v1 behavior", 1, CVarFlags::EditCheckbox);
AutoCVar_Int CVAR_SVSMDynamicPoolSize(CVarCategory::Client | CVarCategory::Rendering, "svsmDynamicPoolSize", "dynamic page pool texture resolution, page count is fixed at startup", 2048);
AutoCVar_Int CVAR_SVSMRenderBudget(CVarCategory::Client | CVarCategory::Rendering, "svsmRenderBudget", "static pages rendered per frame, 0 = unlimited; overflow refines over following frames coarse-to-fine, the coarsest two rings are exempt", 0);
AutoCVar_Float CVAR_SVSMAnimatedCasterRange(CVarCategory::Client | CVarCategory::Rendering, "svsmAnimatedCasterRange", "camera range in meters within which animated doodads (windmills, flags) cast dynamic shadows, beyond it their pose bakes static, 0 disables", 128.0f);
AutoCVar_Int CVAR_SVSMFreeze(CVarCategory::Client | CVarCategory::Rendering, "svsmFreeze", "freeze SVSM page marking and lifecycle to inspect the cached state", 0, CVarFlags::EditCheckbox);
AutoCVar_Int CVAR_SVSMInvalidateAll(CVarCategory::Client | CVarCategory::Rendering, "svsmInvalidateAll", "one shot, invalidate every cached SVSM page", 0, CVarFlags::EditCheckbox);
AutoCVar_Int CVAR_SVSMValidateDynamic(CVarCategory::Client | CVarCategory::Rendering, "svsmValidateDynamic", "one shot, read back the dynamic page table and free list and validate pool invariants (aliasing, leaks)", 0, CVarFlags::EditCheckbox);
AutoCVar_Int CVAR_SVSMDebugClipmap(CVarCategory::Client | CVarCategory::Rendering, "svsmDebugClipmap", "draw this clipmap's page table as an overlay, -1 disables", -1);
AutoCVar_Int CVAR_SVSMDebugShowPool(CVarCategory::Client | CVarCategory::Rendering, "svsmDebugShowPool", "draw a downsampled view of a physical page pool as an overlay: 1 = static pool, 2 = dynamic pool", 0);
AutoCVar_Float CVAR_SVSMZHalfRange(CVarCategory::Client | CVarCategory::Rendering, "svsmZHalfRange", "half depth range of the clipmap windows around the camera in light space, changes invalidate all pages", 2048.0f);
AutoCVar_Float CVAR_SVSMConstantBias(CVarCategory::Client | CVarCategory::Rendering, "svsmConstantBias", "SVSM compare bias toward the sun in world meters, the software depth path has no hardware bias", 0.15f);
AutoCVar_Int CVAR_SVSMProfileGeometry(CVarCategory::Client | CVarCategory::Rendering, "svsmProfileGeometry", "debug: per-view fill/draw GPU time queries in the SVSM geometry passes, shown in the render pass list", 0, CVarFlags::EditCheckbox);
AutoCVar_Int CVAR_SVSMClipRects(CVarCategory::Client | CVarCategory::Rendering, "svsmClipRects", "clip the static page draws to the classified dirty rects (3 draws per view), 0 reverts to one unclipped draw for A/B", 1, CVarFlags::EditCheckbox);

// u32 indices into the flat SVSMData readback, mirrors Shadows/SVSM.inc.slang
namespace SVSMDataOffsets
{
    constexpr u32 Extent = 56;
    constexpr u32 StatsMarked = 112;
    constexpr u32 StatsResident = 120;
    constexpr u32 StatsDirty = 128;
    constexpr u32 StatsEvicted = 136;
    constexpr u32 StatsInvalidated = 144;
    constexpr u32 StatsOverflow = 152;
    constexpr u32 StatsInvalidationCause = 153;
    constexpr u32 StatsFreeListCount = 154;
    constexpr u32 StatsDynamicLive = 196;
    constexpr u32 StatsDynamicOverflow = 204;
    constexpr u32 StatsDynamicTotal = 205;
    constexpr u32 StatsDeferred = 208;
    constexpr u32 StatsBudgetUsed = 216;
}

// SVSM invalidation cause bits, shared with Shadows/SVSM.inc.slang
namespace SVSMCause
{
    constexpr u32 Manual = 4;
    constexpr u32 AABBOverflow = 8;
}

ShadowRenderer::ShadowRenderer(Renderer::Renderer* renderer, GameRenderer* gameRenderer, DebugRenderer* debugRenderer, TerrainRenderer* terrainRenderer, ModelRenderer* modelRenderer, RenderResources& resources)
    : _renderer(renderer)
    , _gameRenderer(gameRenderer)
    , _debugRenderer(debugRenderer)
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
    CreatePermanentResources(resources);
}

ShadowRenderer::~ShadowRenderer()
{
}

void ShadowRenderer::Update(f32 deltaTime, RenderResources& resources)
{
    ZoneScoped;

    _lastDeltaTime = deltaTime;

    // SVSM: last frame's page table stats plus this frame's caster bounds. With the caster split,
    // the current dynamic set (moved + animated) feeds dynamic page marking, and only
    // classification transitions and spawns/despawns invalidate cached static content. With the
    // split off, everything feeds static invalidation like v1
    _svsmDirtyAABBs.clear();
    _svsmDynamicAABBs.clear();
    _svsmDirtyAABBOverflow = false;
    _svsmNumDynamicCasters = 0;
    _svsmNumAnimatedCasters = 0;
    _svsmDynamicAABBsDropped = 0;
    if (CVAR_ShadowEnabled.Get())
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

        u32* svsmData = static_cast<u32*>(_renderer->MapBuffer(_svsmDataReadBackBuffer));
        if (svsmData != nullptr)
        {
            memcpy(_svsmDynamicLivePrev, &_svsmDataReadBack[196], sizeof(_svsmDynamicLivePrev)); // SVSMDataOffsets::StatsDynamicLive
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
                    if (entry == 0 || (entry & (1u << 25)) == 0) // SVSM_PAGE_RESIDENT
                        continue;

                    numResident++;
                    u32 physicalPage = entry & 0xFFFu; // SVSM_PAGE_PHYS_MASK
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

        entt::registry* gameRegistry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        const bool split = CVAR_SVSMDynamicSplit.Get() == 1;

        auto AppendStaticAABB = [this](const vec4& min, const vec4& max)
        {
            if (_svsmDirtyAABBs.size() >= SVSM_MAX_DIRTY_AABBS * 2)
            {
                _svsmDirtyAABBOverflow = true; // Falls back to a full static invalidation
                return;
            }

            _svsmDirtyAABBs.push_back(min);
            _svsmDirtyAABBs.push_back(max);
        };

        // Dynamic list overflow must NOT full-invalidate the static cache: the dropped casters'
        // shadows just freeze for the frame, visible in the perf editor
        auto AppendDynamicAABB = [this](const vec4& min, const vec4& max)
        {
            if (_svsmDynamicAABBs.size() >= SVSM_MAX_DYNAMIC_AABBS * 2)
            {
                _svsmDynamicAABBsDropped++;
                return;
            }

            _svsmDynamicAABBs.push_back(min);
            _svsmDynamicAABBs.push_back(max);
        };

        // Spawned/despawned/re-modeled instances always invalidate cached static pages
        if (_modelRenderer->DrainShadowInvalidations(_svsmDirtyAABBs, SVSM_MAX_DIRTY_AABBS) * 2 > SVSM_MAX_DIRTY_AABBS * 2)
        {
            _svsmDirtyAABBOverflow = true;
        }

        // The current dynamic caster set: moved this frame or actively bone-simulated
        robin_hood::unordered_set<u32> currentDynamicEntities;

        auto CollectDynamic = [&](entt::entity entity, const ECS::Components::Model& model, const ECS::Components::WorldAABB& worldAABB)
        {
            if (model.instanceID == std::numeric_limits<u32>::max())
                return;

            u32 entityHandle = static_cast<u32>(entity);
            if (!currentDynamicEntities.insert(entityHandle).second)
                return; // Already collected through the other view

            vec4 aabbMin = vec4(worldAABB.min, 0.0f);
            vec4 aabbMax = vec4(worldAABB.max, 0.0f);
            if (split)
            {
                AppendDynamicAABB(aabbMin, aabbMax);

                // Newly dynamic: its shadow is baked into static pages and must come out
                if (!_svsmPrevDynamicEntities.contains(entityHandle))
                {
                    AppendStaticAABB(aabbMin, aabbMax);
                }
            }
            else
            {
                AppendStaticAABB(aabbMin, aabbMax); // v1 behavior
            }
        };

        gameRegistry->view<ECS::Components::Model, ECS::Components::WorldAABB, ECS::Components::DirtyTransform>().each([&](entt::entity entity, ECS::Components::Model& model, ECS::Components::WorldAABB& worldAABB, ECS::Components::DirtyTransform&)
        {
            CollectDynamic(entity, model, worldAABB);
        });

        gameRegistry->view<ECS::Components::Model, ECS::Components::WorldAABB, ECS::Components::AnimationData>().each([&](entt::entity entity, ECS::Components::Model& model, ECS::Components::WorldAABB& worldAABB, ECS::Components::AnimationData&)
        {
            CollectDynamic(entity, model, worldAABB);
        });

        // Turned static (or split just toggled off): its last pose must bake back into static pages.
        // Destroyed-while-dynamic entities are covered by the despawn queue above
        for (u32 entityHandle : _svsmPrevDynamicEntities)
        {
            if (currentDynamicEntities.contains(entityHandle))
                continue;

            entt::entity entity = static_cast<entt::entity>(entityHandle);
            if (!gameRegistry->valid(entity) || !gameRegistry->all_of<ECS::Components::WorldAABB>(entity))
                continue;

            const ECS::Components::WorldAABB& worldAABB = gameRegistry->get<ECS::Components::WorldAABB>(entity);
            AppendStaticAABB(vec4(worldAABB.min, 0.0f), vec4(worldAABB.max, 0.0f));
        }

        _svsmNumDynamicCasters = static_cast<u32>(currentDynamicEntities.size());
        _svsmPrevDynamicEntities = std::move(currentDynamicEntities);
        if (!split)
        {
            _svsmPrevDynamicEntities.clear(); // Re-enabling the split re-transitions everything
        }

        // Animated doodads in range, classified and range-diffed by the ModelRenderer. With the
        // split off the ModelRenderer feeds them through the static invalidation queue instead
        // and this list is empty
        const std::vector<vec4>& animatedAABBs = _modelRenderer->GetAnimatedCasterAABBs();
        _svsmNumAnimatedCasters = static_cast<u32>(animatedAABBs.size() / 2);
        for (size_t i = 0; i + 1 < animatedAABBs.size(); i += 2)
        {
            AppendDynamicAABB(animatedAABBs[i], animatedAABBs[i + 1]);
        }

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
        // Shadows off: spawn/despawn invalidations would accumulate in the queue unboundedly.
        // Discard them (maxPairs 0 appends nothing) and re-bake the whole cache on re-enable instead
        if (_modelRenderer->DrainShadowInvalidations(_svsmDirtyAABBs, 0) > 0)
        {
            _svsmForceInvalidateAll = true;
        }
    }
}

void ShadowRenderer::AddSVSMUpdatePass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex)
{
    struct Data
    {
        Renderer::DepthImageResource depth;

        Renderer::BufferMutableResource cameras;
        Renderer::BufferMutableResource svsmDataBuffer;
        Renderer::BufferMutableResource pageTableBuffer;
        Renderer::BufferMutableResource freeListBuffer;
        Renderer::BufferMutableResource dirtyAABBBuffer;
        Renderer::BufferMutableResource clearListBuffer;
        Renderer::BufferMutableResource dynamicPageTableBuffer;
        Renderer::BufferMutableResource dynamicFreeListBuffer;
        Renderer::BufferMutableResource dynamicClearListBuffer;
        Renderer::BufferMutableResource dynamicAABBBuffer;
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
    };

    const u32 numClipmaps = static_cast<u32>(glm::clamp(CVAR_SVSMNumClipmaps.Get(), 1, static_cast<i32>(SVSM_MAX_CLIPMAPS)));
    const bool enabled = CVAR_ShadowEnabled.Get() && !CVAR_SVSMFreeze.Get();
    if (!enabled || _svsmPagePool == Renderer::ImageID::Invalid())
        return;

    // Record-time inputs, the shadow sun steps in discrete intervals so cached pages stay valid
    // between steps
    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
    auto& dayNightCycle = registry->ctx().get<ECS::Singletons::DayNightCycle>();
    const f32 shadowTimeOfDay = ECS::Systems::GetShadowTimeOfDay(dayNightCycle.GetTimeInSecondsF32());
    const vec3 lightDirection = ECS::Systems::UpdateAreaLights::GetLightDirection(shadowTimeOfDay);

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

    renderGraph->AddPass<Data>("SVSM Update",
        [this, &resources](Data& data, Renderer::RenderGraphBuilder& builder)
        {
            using BufferUsage = Renderer::BufferPassUsage;

            data.depth = builder.Read(resources.depth, Renderer::PipelineType::COMPUTE);

            data.cameras = builder.Write(resources.cameras.GetBuffer(), BufferUsage::COMPUTE);
            data.svsmDataBuffer = builder.Write(_svsmDataBuffer, BufferUsage::COMPUTE | BufferUsage::TRANSFER);
            data.pageTableBuffer = builder.Write(_svsmPageTableBuffer, BufferUsage::COMPUTE);
            data.freeListBuffer = builder.Write(_svsmFreeListBuffer, BufferUsage::COMPUTE);
            data.dirtyAABBBuffer = builder.Write(_svsmDirtyAABBBuffer, BufferUsage::COMPUTE);
            data.clearListBuffer = builder.Write(_svsmClearListBuffer, BufferUsage::COMPUTE);
            data.dynamicPageTableBuffer = builder.Write(_svsmDynamicPageTableBuffer, BufferUsage::COMPUTE);
            data.dynamicFreeListBuffer = builder.Write(_svsmDynamicFreeListBuffer, BufferUsage::COMPUTE);
            data.dynamicClearListBuffer = builder.Write(_svsmDynamicClearListBuffer, BufferUsage::COMPUTE);
            data.dynamicAABBBuffer = builder.Write(_svsmDynamicAABBBuffer, BufferUsage::COMPUTE);
            data.svsmDataReadBackBuffer = builder.Write(_svsmDataReadBackBuffer, BufferUsage::TRANSFER);
            data.dynamicValidateReadBackBuffer = builder.Write(_svsmDynamicValidateReadBackBuffer, BufferUsage::TRANSFER);
            data.pagePool = builder.Write(_svsmPagePool, Renderer::PipelineType::COMPUTE, Renderer::LoadMode::LOAD);
            data.dynamicPagePool = builder.Write(_svsmDynamicPagePool, Renderer::PipelineType::COMPUTE, Renderer::LoadMode::LOAD);

            data.prepareSet = builder.Use(_svsmPrepareDescriptorSet);
            data.invalidateSet = builder.Use(_svsmInvalidateAABBsDescriptorSet);
            data.updateASet = builder.Use(_svsmPageUpdateADescriptorSet);
            data.markSet = builder.Use(_svsmPageMarkDescriptorSet);
            data.updateBSet = builder.Use(_svsmPageUpdateBDescriptorSet);
            data.dynamicMarkSet = builder.Use(_svsmDynamicMarkDescriptorSet);
            data.dynamicUpdateSet = builder.Use(_svsmDynamicUpdateDescriptorSet);
            data.finalizeSet = builder.Use(_svsmFinalizeDescriptorSet);
            data.clearSet = builder.Use(_svsmPageClearDescriptorSet);
            data.dynamicClearSet = builder.Use(_svsmDynamicPageClearDescriptorSet);

            return true; // Return true from setup to enable this pass, return false to disable it
        },
        [this, frameIndex, numClipmaps, lightDirection, invalidateCause, numDirtyAABBs, dynamicSplit, numDynamicAABBs](Data& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList)
        {
            GPU_SCOPED_PROFILER_ZONE(commandList, SVSMUpdate);

            struct SVSMConstants
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
                f32 padding0;
                f32 padding1;
                f32 resolutionScale;
                u32 dynamicPoolPagesPerRow;
                u32 renderBudget;
                u32 dynamicPhase;
                u32 padding3;
                u32 padding4;
            };

            CVarSystem* cvarSystem = CVarSystem::Get();
            const u32 pageSize = static_cast<u32>(glm::max(CVAR_SVSMPageSize.Get(), 16));
            const u32 pageTableSize = glm::clamp(static_cast<u32>(CVAR_SVSMVirtualSize.Get()) / pageSize, 16u, SVSM_MAX_PAGE_TABLE_SIZE);
            u32 poolPagesPerRow = static_cast<u32>(CVAR_SVSMPoolSize.Get()) / pageSize;
            poolPagesPerRow = glm::min(poolPagesPerRow, SVSM_MAX_PAGE_TABLE_SIZE);
            u32 dynamicPoolPagesPerRow = static_cast<u32>(CVAR_SVSMDynamicPoolSize.Get()) / pageSize;
            dynamicPoolPagesPerRow = glm::min(dynamicPoolPagesPerRow, SVSM_MAX_PAGE_TABLE_SIZE);

            SVSMConstants* constants = graphResources.FrameNew<SVSMConstants>();
            constants->lightDirection = vec4(lightDirection, 0.0f);
            constants->numClipmaps = numClipmaps;
            constants->pageTableSize = pageTableSize;
            constants->pageSize = pageSize;
            constants->poolPagesPerRow = poolPagesPerRow;
            constants->clipmap0Extent = CVAR_SVSMClipmap0Extent.GetFloat();
            constants->markBorderTexels = CVAR_SVSMMarkBorderTexels.GetFloat();
            constants->evictAge = static_cast<u32>(glm::clamp(CVAR_SVSMPageEvictAge.Get(), 1, 254));
            constants->invalidateAll = invalidateCause;
            constants->numDirtyAABBs = numDirtyAABBs;
            constants->allocClipmap = 0;
            constants->casterMargin = static_cast<f32>(*cvarSystem->GetFloatCVar(CVarCategory::Client | CVarCategory::Rendering, "shadowCasterMargin"));
            constants->zHalfRange = CVAR_SVSMZHalfRange.GetFloat();
            constants->padding0 = 0.0f;
            constants->padding1 = 0.0f;
            constants->resolutionScale = CVAR_SVSMResolutionScale.GetFloat();
            constants->dynamicPoolPagesPerRow = dynamicSplit ? dynamicPoolPagesPerRow : 0;
            constants->renderBudget = static_cast<u32>(glm::max(CVAR_SVSMRenderBudget.Get(), 0));
            constants->dynamicPhase = 0;

            if (_svsmPoolNeedsClear)
            {
                // One-shot zero of the fresh pools: uninitialized VRAM must never be sampled, and
                // depth atomics against garbage would keep the garbage
                commandList.Clear(data.pagePool, uvec4(0, 0, 0, 0));
                commandList.ImageBarrier(data.pagePool);
                commandList.Clear(data.dynamicPagePool, uvec4(0, 0, 0, 0));
                commandList.ImageBarrier(data.dynamicPagePool);
                _svsmPoolNeedsClear = false;
            }

            // The AABB lists upload from Update through the frame-synced staging ring, the render
            // graph issues a global upload barrier before any pass executes

            // Prepare: invalidation detection, window anchors, per-frame stat reset
            commandList.BeginPipeline(_svsmPreparePipeline);
            commandList.PushConstant(constants, 0, sizeof(SVSMConstants));

            commandList.BindDescriptorSet(data.prepareSet, frameIndex);

            commandList.Dispatch(1, 1, 1);
            commandList.EndPipeline(_svsmPreparePipeline);

            commandList.BufferBarrier(data.svsmDataBuffer, Renderer::BufferPassUsage::COMPUTE);

            // Moved and animated instances invalidate the pages under them
            if (numDirtyAABBs > 0)
            {
                commandList.BeginPipeline(_svsmInvalidateAABBsPipeline);
                commandList.PushConstant(constants, 0, sizeof(SVSMConstants));
                commandList.BindDescriptorSet(data.invalidateSet, frameIndex);

                commandList.Dispatch((numDirtyAABBs + 63) / 64, numClipmaps, 1);
                commandList.EndPipeline(_svsmInvalidateAABBsPipeline);

                commandList.BufferBarrier(data.pageTableBuffer, Renderer::BufferPassUsage::COMPUTE);
                commandList.BufferBarrier(data.svsmDataBuffer, Renderer::BufferPassUsage::COMPUTE);
            }

            // Update A: toroidal and global invalidation, aging, eviction. Visits the full table
            // capacity so entries orphaned by config changes still age out
            const u32 tableCapacity = SVSM_MAX_CLIPMAPS * SVSM_MAX_PAGE_TABLE_SIZE * SVSM_MAX_PAGE_TABLE_SIZE;

            commandList.BeginPipeline(_svsmPageUpdateAPipeline);
            commandList.PushConstant(constants, 0, sizeof(SVSMConstants));
            commandList.BindDescriptorSet(data.updateASet, frameIndex);

            commandList.Dispatch(tableCapacity / 256, 1, 1);
            commandList.EndPipeline(_svsmPageUpdateAPipeline);

            commandList.BufferBarrier(data.pageTableBuffer, Renderer::BufferPassUsage::COMPUTE);
            commandList.BufferBarrier(data.freeListBuffer, Renderer::BufferPassUsage::COMPUTE);

            // Mark: pages touched by visible depth samples
            commandList.BeginPipeline(_svsmPageMarkPipeline);
            commandList.PushConstant(constants, 0, sizeof(SVSMConstants));

            data.markSet.Bind("_depth"_h, data.depth);
            commandList.BindDescriptorSet(data.markSet, frameIndex);

            uvec2 depthDimensions = graphResources.GetImageDimensions(data.depth);
            commandList.Dispatch((depthDimensions.x + 15) / 16, (depthDimensions.y + 15) / 16, 1);

            commandList.EndPipeline(_svsmPageMarkPipeline);

            commandList.BufferBarrier(data.pageTableBuffer, Renderer::BufferPassUsage::COMPUTE);

            // Update B: allocation, re-dirtying, dirty rects. One dispatch per clipmap, finest
            // first with a free list barrier between, so pool starvation always lands on the
            // coarsest ring where the sampler fallback costs the least
            commandList.BeginPipeline(_svsmPageUpdateBPipeline);
            commandList.BindDescriptorSet(data.updateBSet, frameIndex);

            for (u32 clipmapIndex = 0; clipmapIndex < numClipmaps; clipmapIndex++)
            {
                SVSMConstants* allocConstants = graphResources.FrameNew<SVSMConstants>();
                *allocConstants = *constants;
                allocConstants->allocClipmap = clipmapIndex;

                commandList.PushConstant(allocConstants, 0, sizeof(SVSMConstants));
                commandList.Dispatch((pageTableSize * pageTableSize) / 256, 1, 1);

                if (clipmapIndex + 1 < numClipmaps)
                {
                    commandList.BufferBarrier(data.freeListBuffer, Renderer::BufferPassUsage::COMPUTE);
                }
            }

            commandList.EndPipeline(_svsmPageUpdateBPipeline);

            commandList.BufferBarrier(data.pageTableBuffer, Renderer::BufferPassUsage::COMPUTE);
            commandList.BufferBarrier(data.svsmDataBuffer, Renderer::BufferPassUsage::COMPUTE);
            commandList.BufferBarrier(data.clearListBuffer, Renderer::BufferPassUsage::COMPUTE);

            // Caster split: mark pages under dynamic casters (visible ones only), then run the
            // transient dynamic page lifecycle. Must precede Finalize, which unions the rects
            if (dynamicSplit)
            {
                if (numDynamicAABBs > 0)
                {
                    SVSMConstants* dynamicMarkConstants = graphResources.FrameNew<SVSMConstants>();
                    *dynamicMarkConstants = *constants;
                    dynamicMarkConstants->numDirtyAABBs = numDynamicAABBs;

                    commandList.BeginPipeline(_svsmDynamicMarkPipeline);
                    commandList.PushConstant(dynamicMarkConstants, 0, sizeof(SVSMConstants));
                    commandList.BindDescriptorSet(data.dynamicMarkSet, frameIndex);

                    commandList.Dispatch((numDynamicAABBs + 63) / 64, numClipmaps, 1);
                    commandList.EndPipeline(_svsmDynamicMarkPipeline);

                    commandList.BufferBarrier(data.dynamicPageTableBuffer, Renderer::BufferPassUsage::COMPUTE);
                }

                // Two dispatches: release then acquire. Free-list pushes and pops in one dispatch
                // race (a pop can read a slot before the pushed phys index lands), aliasing one
                // physical page under two table entries — the ghost-shadow bug
                commandList.BeginPipeline(_svsmDynamicUpdatePipeline);
                commandList.PushConstant(constants, 0, sizeof(SVSMConstants)); // dynamicPhase 0
                commandList.BindDescriptorSet(data.dynamicUpdateSet, frameIndex);
                commandList.Dispatch(tableCapacity / 256, 1, 1);

                commandList.BufferBarrier(data.dynamicPageTableBuffer, Renderer::BufferPassUsage::COMPUTE);
                commandList.BufferBarrier(data.dynamicFreeListBuffer, Renderer::BufferPassUsage::COMPUTE);

                SVSMConstants* dynamicAcquireConstants = graphResources.FrameNew<SVSMConstants>();
                *dynamicAcquireConstants = *constants;
                dynamicAcquireConstants->dynamicPhase = 1;

                commandList.PushConstant(dynamicAcquireConstants, 0, sizeof(SVSMConstants));
                commandList.Dispatch(tableCapacity / 256, 1, 1);
                commandList.EndPipeline(_svsmDynamicUpdatePipeline);

                commandList.BufferBarrier(data.dynamicPageTableBuffer, Renderer::BufferPassUsage::COMPUTE);
                commandList.BufferBarrier(data.dynamicFreeListBuffer, Renderer::BufferPassUsage::COMPUTE);
                commandList.BufferBarrier(data.dynamicClearListBuffer, Renderer::BufferPassUsage::COMPUTE);
                commandList.BufferBarrier(data.svsmDataBuffer, Renderer::BufferPassUsage::COMPUTE);
            }

            // Finalize: clipmap render cameras from the anchors and this frame's dirty rects
            commandList.BeginPipeline(_svsmFinalizePipeline);
            commandList.PushConstant(constants, 0, sizeof(SVSMConstants));

            commandList.BindDescriptorSet(data.finalizeSet, frameIndex);

            commandList.Dispatch(1, 1, 1);
            commandList.EndPipeline(_svsmFinalizePipeline);

            commandList.BufferBarrier(data.cameras, Renderer::BufferPassUsage::COMPUTE);

            // Clear this frame's dirty pages, one workgroup per page from the list UpdateB built
            commandList.BeginPipeline(_svsmPageClearPipeline);
            commandList.PushConstant(constants, 0, sizeof(SVSMConstants));

            data.clearSet.Bind("_pagePool"_h, data.pagePool);
            commandList.BindDescriptorSet(data.clearSet, frameIndex);

            commandList.DispatchIndirect(data.clearListBuffer, 0);
            commandList.EndPipeline(_svsmPageClearPipeline);

            // Caster split: every live dynamic page clears (and later re-renders) each frame.
            // Same shader, dynamic list + pool, pool row count overridden
            if (dynamicSplit)
            {
                SVSMConstants* dynamicClearConstants = graphResources.FrameNew<SVSMConstants>();
                *dynamicClearConstants = *constants;
                dynamicClearConstants->poolPagesPerRow = constants->dynamicPoolPagesPerRow;

                commandList.BeginPipeline(_svsmPageClearPipeline);
                commandList.PushConstant(dynamicClearConstants, 0, sizeof(SVSMConstants));

                data.dynamicClearSet.Bind("_pagePool"_h, data.dynamicPagePool);
                commandList.BindDescriptorSet(data.dynamicClearSet, frameIndex);

                commandList.DispatchIndirect(data.dynamicClearListBuffer, 0);
                commandList.EndPipeline(_svsmPageClearPipeline);
            }

            commandList.CopyBuffer(data.svsmDataReadBackBuffer, 0, data.svsmDataBuffer, 0, sizeof(u32) * SVSM_DATA_UINT_COUNT);

            // One-shot dynamic pool validation snapshot, Update maps and checks it next frame
            if (dynamicSplit && !_svsmValidatePending && CVAR_SVSMValidateDynamic.Get() != 0)
            {
                const u32 tableUints = SVSM_MAX_CLIPMAPS * SVSM_MAX_PAGE_TABLE_SIZE * SVSM_MAX_PAGE_TABLE_SIZE;
                commandList.CopyBuffer(data.dynamicValidateReadBackBuffer, 0, data.dynamicPageTableBuffer, 0, sizeof(u32) * tableUints);
                commandList.CopyBuffer(data.dynamicValidateReadBackBuffer, sizeof(u32) * tableUints, data.dynamicFreeListBuffer, 0, sizeof(u32) * (4 + SVSM_MAX_POOL_PAGES));
                _svsmValidatePending = true;
            }
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

                const u32 pageSize = static_cast<u32>(glm::max(CVAR_SVSMPageSize.Get(), 16));
                const u32 pageTableSize = glm::clamp(static_cast<u32>(CVAR_SVSMVirtualSize.Get()) / pageSize, 16u, SVSM_MAX_PAGE_TABLE_SIZE);
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

        // The physical page index is 12 bits in the table entry, the pool page counts are fixed at
        // startup (svsmPoolSize/svsmDynamicPoolSize changes need a restart)
        const u32 pageSize = static_cast<u32>(glm::max(CVAR_SVSMPageSize.Get(), 16));
        const u32 poolPagesPerRow = static_cast<u32>(CVAR_SVSMPoolSize.Get()) / pageSize;
        _svsmPoolPages = glm::min(poolPagesPerRow * poolPagesPerRow, SVSM_MAX_POOL_PAGES);
        const u32 dynamicPoolPagesPerRow = static_cast<u32>(CVAR_SVSMDynamicPoolSize.Get()) / pageSize;
        _svsmDynamicPoolPages = glm::min(dynamicPoolPagesPerRow * dynamicPoolPagesPerRow, SVSM_MAX_POOL_PAGES);

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

        bufferDesc.name = "SVSMDirtyAABBs";
        bufferDesc.size = sizeof(vec4) * SVSM_MAX_DIRTY_AABBS * 2;
        bufferDesc.usage = Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION;
        _svsmDirtyAABBBuffer = _renderer->CreateBuffer(_svsmDirtyAABBBuffer, bufferDesc);

        bufferDesc.name = "SVSMDynamicAABBs";
        bufferDesc.size = sizeof(vec4) * SVSM_MAX_DYNAMIC_AABBS * 2;
        _svsmDynamicAABBBuffer = _renderer->CreateBuffer(_svsmDynamicAABBBuffer, bufferDesc);

        // Static/dynamic caster split resources
        bufferDesc.name = "SVSMDynamicPageTable";
        bufferDesc.size = sizeof(u32) * SVSM_MAX_CLIPMAPS * SVSM_MAX_PAGE_TABLE_SIZE * SVSM_MAX_PAGE_TABLE_SIZE;
        bufferDesc.usage = Renderer::BufferUsage::STORAGE_BUFFER;
        _svsmDynamicPageTableBuffer = _renderer->CreateAndFillBuffer(_svsmDynamicPageTableBuffer, bufferDesc, [](void* mappedMemory, size_t size)
        {
            memset(mappedMemory, 0, size);
        });

        bufferDesc.name = "SVSMDynamicFreeList";
        bufferDesc.size = sizeof(u32) * (4 + SVSM_MAX_POOL_PAGES);
        bufferDesc.usage = Renderer::BufferUsage::STORAGE_BUFFER;
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

        _svsmPrepareDescriptorSet.Bind("_svsmData"_h, _svsmDataBuffer);
        _svsmPrepareDescriptorSet.Bind("_freeList"_h, _svsmFreeListBuffer);
        _svsmPrepareDescriptorSet.Bind("_clearList"_h, _svsmClearListBuffer);
        _svsmPrepareDescriptorSet.Bind("_dynamicClearList"_h, _svsmDynamicClearListBuffer);
        _svsmInvalidateAABBsDescriptorSet.Bind("_svsmData"_h, _svsmDataBuffer);
        _svsmInvalidateAABBsDescriptorSet.Bind("_pageTable"_h, _svsmPageTableBuffer);
        _svsmInvalidateAABBsDescriptorSet.Bind("_dirtyAABBs"_h, _svsmDirtyAABBBuffer);
        _svsmPageUpdateADescriptorSet.Bind("_svsmData"_h, _svsmDataBuffer);
        _svsmPageUpdateADescriptorSet.Bind("_pageTable"_h, _svsmPageTableBuffer);
        _svsmPageUpdateADescriptorSet.Bind("_freeList"_h, _svsmFreeListBuffer);
        _svsmPageMarkDescriptorSet.Bind("_svsmData"_h, _svsmDataBuffer);
        _svsmPageMarkDescriptorSet.Bind("_pageTable"_h, _svsmPageTableBuffer);
        _svsmPageUpdateBDescriptorSet.Bind("_svsmData"_h, _svsmDataBuffer);
        _svsmPageUpdateBDescriptorSet.Bind("_pageTable"_h, _svsmPageTableBuffer);
        _svsmPageUpdateBDescriptorSet.Bind("_freeList"_h, _svsmFreeListBuffer);
        _svsmPageUpdateBDescriptorSet.Bind("_clearList"_h, _svsmClearListBuffer);
        _svsmDynamicMarkDescriptorSet.Bind("_svsmData"_h, _svsmDataBuffer);
        _svsmDynamicMarkDescriptorSet.Bind("_pageTable"_h, _svsmPageTableBuffer);
        _svsmDynamicMarkDescriptorSet.Bind("_dynamicPageTable"_h, _svsmDynamicPageTableBuffer);
        _svsmDynamicMarkDescriptorSet.Bind("_dynamicAABBs"_h, _svsmDynamicAABBBuffer);
        _svsmDynamicUpdateDescriptorSet.Bind("_svsmData"_h, _svsmDataBuffer);
        _svsmDynamicUpdateDescriptorSet.Bind("_dynamicPageTable"_h, _svsmDynamicPageTableBuffer);
        _svsmDynamicUpdateDescriptorSet.Bind("_dynamicFreeList"_h, _svsmDynamicFreeListBuffer);
        _svsmDynamicUpdateDescriptorSet.Bind("_dynamicClearList"_h, _svsmDynamicClearListBuffer);
        _svsmFinalizeDescriptorSet.Bind("_svsmData"_h, _svsmDataBuffer);
        _svsmPageClearDescriptorSet.Bind("_clearList"_h, _svsmClearListBuffer);
        _svsmDynamicPageClearDescriptorSet.Bind("_clearList"_h, _svsmDynamicClearListBuffer);
        _svsmPageTableDebugDescriptorSet.Bind("_pageTable"_h, _svsmPageTableBuffer);
        // _srcCameras / _depth / _target / _rwCameras / _pagePool are bound per frame, those resources can be recreated

        bufferDesc.name = "SVSMDataReadBack";
        bufferDesc.size = sizeof(u32) * SVSM_DATA_UINT_COUNT;
        bufferDesc.usage = Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION;
        bufferDesc.cpuAccess = Renderer::BufferCPUAccess::ReadOnly;
        _svsmDataReadBackBuffer = _renderer->CreateBuffer(_svsmDataReadBackBuffer, bufferDesc);

        bufferDesc.name = "SVSMDynamicValidateReadBack";
        bufferDesc.size = sizeof(u32) * (SVSM_MAX_CLIPMAPS * SVSM_MAX_PAGE_TABLE_SIZE * SVSM_MAX_PAGE_TABLE_SIZE + 4 + SVSM_MAX_POOL_PAGES);
        _svsmDynamicValidateReadBackBuffer = _renderer->CreateBuffer(_svsmDynamicValidateReadBackBuffer, bufferDesc);

        // The material pass samples SVSM through the LIGHT set. The buffers bind here, the pools
        // bind per frame in the material pass (real pools once created, a placeholder before)
        resources.lightDescriptorSet.Bind("_svsmData"_h, _svsmDataBuffer);
        resources.lightDescriptorSet.Bind("_svsmPageTable"_h, _svsmPageTableBuffer);
        resources.lightDescriptorSet.Bind("_svsmDynamicPageTable"_h, _svsmDynamicPageTableBuffer);

        Renderer::ImageDesc placeholderDesc;
        placeholderDesc.debugName = "SVSMPagePoolPlaceholder";
        placeholderDesc.dimensions = vec2(1, 1);
        placeholderDesc.dimensionType = Renderer::ImageDimensionType::DIMENSION_ABSOLUTE;
        placeholderDesc.format = Renderer::ImageFormat::R32_UINT;
        placeholderDesc.sampleCount = Renderer::SampleCount::SAMPLE_COUNT_1;
        placeholderDesc.clearUInts = uvec4(0, 0, 0, 0);

        _svsmPagePoolPlaceholder = _renderer->CreateImage(placeholderDesc);

        // The terrain/model SVSM page-render sets read these permanent buffers, bind them once
        // here (the pools stay per-frame, they are created lazily). Same for the cameras buffer
        _terrainRenderer->BindSVSMBuffers(_svsmDataBuffer, _svsmPageTableBuffer);
        _modelRenderer->BindSVSMBuffers(_svsmDataBuffer, _svsmPageTableBuffer, _svsmDynamicPageTableBuffer);
    }

    BindCameraBuffers(resources);
}

void ShadowRenderer::BindCameraBuffers(RenderResources& resources)
{
    Renderer::BufferID camerasBuffer = resources.cameras.GetBuffer();

    _svsmPrepareDescriptorSet.Bind("_srcCameras"_h, camerasBuffer);
    _svsmPageMarkDescriptorSet.Bind("_srcCameras"_h, camerasBuffer);
    _svsmFinalizeDescriptorSet.Bind("_rwCameras"_h, camerasBuffer);
}