#include "ShadowRenderer.h"
#include <Game-Lib/Application/EnttRegistries.h>
#include <Game-Lib/ECS/Components/AABB.h>
#include <Game-Lib/ECS/Components/AnimationData.h>
#include <Game-Lib/ECS/Components/Camera.h>
#include <Game-Lib/ECS/Components/Model.h>
#include <Game-Lib/ECS/Singletons/DayNightCycle.h>
#include <Game-Lib/ECS/Util/Transforms.h>
#include <Game-Lib/ECS/Systems/CalculateShadowCameraMatrices.h>
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

AutoCVar_Int CVAR_ShadowDebugMatrices(CVarCategory::Client | CVarCategory::Rendering, "shadowDebugMatrices", "debug shadow matrices by applying them to the camera", 0, CVarFlags::EditCheckbox);
AutoCVar_Int CVAR_ShadowDebugMatrixIndex(CVarCategory::Client | CVarCategory::Rendering, "shadowDebugMatricesIndex", "index of the cascade to debug", 0);

AutoCVar_Int CVAR_ShadowDrawMatrices(CVarCategory::Client | CVarCategory::Rendering, "shadowDrawMatrices", "debug shadow matrices by debug drawing them", 0, CVarFlags::EditCheckbox);

AutoCVar_Int CVAR_ShadowFilterMode(CVarCategory::Client | CVarCategory::Rendering, "shadowFilterMode", "0: No filtering, 1: Percentage Closer Filtering, 2: Percentage Closer Soft Shadows", 1);
AutoCVar_Float CVAR_ShadowFilterSize(CVarCategory::Client | CVarCategory::Rendering, "shadowFilterSize", "size of the filter used for shadow sampling", 3.0f);
AutoCVar_Float CVAR_ShadowFilterPenumbraSize(CVarCategory::Client | CVarCategory::Rendering, "shadowFilterPenumbraSize", "size of the filter used for penumbra sampling", 3.0f);

AutoCVar_Float CVAR_ShadowDepthBiasConstantFactor(CVarCategory::Client | CVarCategory::Rendering, "shadowDepthBiasConstant", "constant factor of depth bias to prevent shadow acne", -2.0f);
AutoCVar_Float CVAR_ShadowDepthBiasClamp(CVarCategory::Client | CVarCategory::Rendering, "shadowDepthBiasClamp", "clamp of depth bias to prevent shadow acne", 0.0f);
AutoCVar_Float CVAR_ShadowDepthBiasSlopeFactor(CVarCategory::Client | CVarCategory::Rendering, "shadowDepthBiasSlope", "slope factor of depth bias to prevent shadow acne", -5.0f);
AutoCVar_Float CVAR_ShadowNormalOffsetBias(CVarCategory::Client | CVarCategory::Rendering, "shadowNormalOffsetBias", "receiver offset along the surface normal in cascade texels, fights acne on hard angles", 1.0f);

AutoCVar_Int CVAR_ShadowUseSDSM(CVarCategory::Client | CVarCategory::Rendering, "shadowUseSDSM", "fit cascade cameras on the GPU instead of the legacy CPU path", 1, CVarFlags::EditCheckbox);
AutoCVar_Int CVAR_ShadowSDSMUseDepthBounds(CVarCategory::Client | CVarCategory::Rendering, "shadowSDSMUseDepthBounds", "distribute cascades over the visible depth range instead of the full shadow range", 1, CVarFlags::EditCheckbox);
AutoCVar_Float CVAR_ShadowSDSMQuantizeStep(CVarCategory::Client | CVarCategory::Rendering, "shadowSDSMQuantizeStep", "SDSM bounds are quantized outward to this step to keep stable snapping effective", 8.0f);
AutoCVar_Float CVAR_ShadowSDSMShrinkDelay(CVarCategory::Client | CVarCategory::Rendering, "shadowSDSMShrinkDelay", "seconds the visible range must stay smaller before the SDSM bounds shrink to it in one jump, expansion is instant", 2.5f);
AutoCVar_Int CVAR_ShadowSDSMValidateParity(CVarCategory::Client | CVarCategory::Rendering, "shadowSDSMValidateParity", "compare GPU-fitted cascade cameras against the CPU math and log the max delta", 0, CVarFlags::EditCheckbox);
AutoCVar_Int CVAR_ShadowSDSMUseXYBounds(CVarCategory::Client | CVarCategory::Rendering, "shadowSDSMUseXYBounds", "fit cascades to the light-space footprint of visible samples: 0 = off, 1 = diagnostics only, 2 = drive the cameras", 2);
AutoCVar_Float CVAR_ShadowSDSMXYMarginTexels(CVarCategory::Client | CVarCategory::Rendering, "shadowSDSMXYMarginTexels", "filter-kernel margin in texels added to the fitted XY bounds, the normal offset bias is added on top", 4.0f);

AutoCVar_Int CVAR_ShadowTechnique(CVarCategory::Client | CVarCategory::Rendering, "shadowTechnique", "0 = cascaded shadow maps (SDSM), 1 = sparse virtual shadow maps", 0);
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
    , _depthMinMaxDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS)
    , _cascadeFitRangeDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS)
    , _cascadeXYReduceDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS)
    , _cascadeFitCamerasDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS)
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

    // Read back last frame's reduced depth bounds for diagnostics
    {
        u32* readBackData = static_cast<u32*>(_renderer->MapBuffer(_depthMinMaxReadBackBuffer));
        if (readBackData != nullptr)
        {
            _depthMinMaxReadBack[0] = readBackData[0];
            _depthMinMaxReadBack[1] = readBackData[1];
        }
        _renderer->UnmapBuffer(_depthMinMaxReadBackBuffer);
    }

    _lastDeltaTime = deltaTime;

    CVarSystem* cvarSystem = CVarSystem::Get();
    const u32 numCascades = static_cast<u32>(*cvarSystem->GetIntCVar(CVarCategory::Client | CVarCategory::Rendering, "shadowCascadeNum"));
    const bool useSDSM = CVAR_ShadowUseSDSM.Get();

    // With SDSM the cascade cameras live on the GPU, the debug tools read the (one frame old) readback copies
    if (useSDSM)
    {
        Camera* readBackData = static_cast<Camera*>(_renderer->MapBuffer(_cascadeCamerasReadBackBuffer));
        if (readBackData != nullptr)
        {
            memcpy(_readBackCascadeCameras, readBackData, sizeof(Camera) * Renderer::Settings::MAX_SHADOW_CASCADES);
        }
        _renderer->UnmapBuffer(_cascadeCamerasReadBackBuffer);

        f32* sdsmData = static_cast<f32*>(_renderer->MapBuffer(_sdsmDataReadBackBuffer));
        if (sdsmData != nullptr)
        {
            memcpy(_sdsmDataReadBack, sdsmData, sizeof(f32) * SDSM_DATA_FLOAT_COUNT);
        }
        _renderer->UnmapBuffer(_sdsmDataReadBackBuffer);
    }

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
    if (CVAR_ShadowTechnique.Get() == 1 && CVAR_ShadowEnabled.Get())
    {
        // The pools are the SVSM VRAM cost, only allocated once the technique is actually used
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
        // SVSM inactive: spawn/despawn invalidations would accumulate in the queue unboundedly.
        // Discard them (maxPairs 0 appends nothing) and re-bake the whole cache when the
        // technique comes back instead
        if (_modelRenderer->DrainShadowInvalidations(_svsmDirtyAABBs, 0) > 0)
        {
            _svsmForceInvalidateAll = true;
        }
    }

    auto GetCascadeCamera = [&](u32 cascadeIndex) -> const Camera&
    {
        return useSDSM ? _readBackCascadeCameras[cascadeIndex] : resources.cameras[cascadeIndex + 1];
    };

    if (CVAR_ShadowSDSMValidateParity.Get() && useSDSM)
    {
        // The CPU mirror holds the legacy math result while validation is on (computed but not uploaded).
        // Only meaningful with a static camera, the readback is one frame old
        f32 maxDelta = 0.0f;
        for (u32 i = 0; i < numCascades; i++)
        {
            const mat4x4& gpuMatrix = _readBackCascadeCameras[i].worldToClip;
            const mat4x4& cpuMatrix = resources.cameras[i + 1].worldToClip;

            for (u32 col = 0; col < 4; col++)
            {
                for (u32 row = 0; row < 4; row++)
                {
                    maxDelta = Math::Max(maxDelta, glm::abs(gpuMatrix[col][row] - cpuMatrix[col][row]));
                }
            }
        }

        static u32 parityLogCounter = 0;
        if (parityLogCounter++ % 60 == 0)
        {
            NC_LOG_INFO("SDSM Parity: max worldToClip delta {0} across {1} cascades (stand still, readback is one frame old)", maxDelta, numCascades);
        }
    }

    const bool debugMatrices = CVAR_ShadowDebugMatrices.Get();
    const i32 debugMatrixIndex = CVAR_ShadowDebugMatrixIndex.Get();
    if (debugMatrices && debugMatrixIndex >= 0 && debugMatrixIndex < static_cast<i32>(numCascades))
    {
        const Camera& debugCascadeCamera = GetCascadeCamera(debugMatrixIndex);

        Camera& mainCamera = resources.cameras[0];

        mainCamera = debugCascadeCamera;
        resources.cameras.SetDirtyElement(0);
    }

    if (CVAR_ShadowDrawMatrices.Get())
    {
        Color colors[] =
        {
            Color::Red,
            Color::Green,
            Color::Blue,
            Color::Yellow,
            Color::Magenta,
            Color::Cyan,
            Color::PastelOrange,
            Color::PastelGreen
        };

        for (u32 i = 0; i < numCascades; i++)
        {
            const Camera& debugCascadeCamera = GetCascadeCamera(i);
            _debugRenderer->DrawFrustum(debugCascadeCamera.worldToClip, colors[i]);
        }
    }
}

void ShadowRenderer::AddDepthMinMaxPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex)
{
    struct Data
    {
        Renderer::DepthImageResource depth;

        Renderer::BufferMutableResource depthMinMaxBuffer;
        Renderer::BufferMutableResource depthMinMaxReadBackBuffer;

        Renderer::DescriptorSetResource passSet;
    };

    CVarSystem* cvarSystem = CVarSystem::Get();
    const u32 numCascades = static_cast<u32>(*cvarSystem->GetIntCVar(CVarCategory::Client | CVarCategory::Rendering, "shadowCascadeNum"));
    const bool dispatchEnabled = CVAR_ShadowEnabled.Get() && numCascades > 0;

    renderGraph->AddPass<Data>("Shadow Depth MinMax",
        [this, &resources](Data& data, Renderer::RenderGraphBuilder& builder)
        {
            using BufferUsage = Renderer::BufferPassUsage;

            data.depth = builder.Read(resources.depth, Renderer::PipelineType::COMPUTE);

            data.depthMinMaxBuffer = builder.Write(_depthMinMaxBuffer, BufferUsage::TRANSFER | BufferUsage::COMPUTE);
            data.depthMinMaxReadBackBuffer = builder.Write(_depthMinMaxReadBackBuffer, BufferUsage::TRANSFER);

            data.passSet = builder.Use(_depthMinMaxDescriptorSet);

            return true; // Return true from setup to enable this pass, return false to disable it
        },
        [this, frameIndex, dispatchEnabled](Data& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList)
        {
            GPU_SCOPED_PROFILER_ZONE(commandList, ShadowDepthMinMax);

            // Reset to the sentinel, if the dispatch is skipped or every pixel is sky it survives
            // and the consumers fall back to the full range
            commandList.FillBuffer(data.depthMinMaxBuffer, 0, sizeof(u32), 0xFFFFFFFF);
            commandList.FillBuffer(data.depthMinMaxBuffer, sizeof(u32), sizeof(u32), 0);
            commandList.BufferBarrier(data.depthMinMaxBuffer, Renderer::BufferPassUsage::TRANSFER);

            if (dispatchEnabled)
            {
                commandList.BeginPipeline(_depthMinMaxPipeline);

                data.passSet.Bind("_depth"_h, data.depth);
                commandList.BindDescriptorSet(data.passSet, frameIndex);

                uvec2 depthDimensions = graphResources.GetImageDimensions(data.depth);
                commandList.Dispatch((depthDimensions.x + 15) / 16, (depthDimensions.y + 15) / 16, 1);

                commandList.EndPipeline(_depthMinMaxPipeline);

                commandList.BufferBarrier(data.depthMinMaxBuffer, Renderer::BufferPassUsage::COMPUTE);
            }

            commandList.CopyBuffer(data.depthMinMaxReadBackBuffer, 0, data.depthMinMaxBuffer, 0, sizeof(u32) * 2);
        });
}

void ShadowRenderer::AddCascadeFitPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex)
{
    struct Data
    {
        Renderer::DepthImageResource depth;

        Renderer::BufferMutableResource cameras;
        Renderer::BufferMutableResource sdsmDataBuffer;
        Renderer::BufferMutableResource cascadeBoundsBuffer;
        Renderer::BufferMutableResource sdsmDataReadBackBuffer;
        Renderer::BufferMutableResource cascadeCamerasReadBackBuffer;

        Renderer::DescriptorSetResource rangeSet;
        Renderer::DescriptorSetResource reduceSet;
        Renderer::DescriptorSetResource camerasSet;
    };

    CVarSystem* cvarSystem = CVarSystem::Get();
    u32 numCascades = static_cast<u32>(*cvarSystem->GetIntCVar(CVarCategory::Client | CVarCategory::Rendering, "shadowCascadeNum"));
    numCascades = std::min(numCascades, static_cast<u32>(resources.shadowDepthCascades.size()));

    const bool freezeCascades = *cvarSystem->GetIntCVar(CVarCategory::Client | CVarCategory::Rendering, "shadowFreezeCascades") != 0;
    const bool useSVSM = CVAR_ShadowTechnique.Get() == 1; // SVSM Finalize owns the camera slots instead
    const bool dispatchEnabled = CVAR_ShadowUseSDSM.Get() && CVAR_ShadowEnabled.Get() && numCascades > 0 && !freezeCascades && !useSVSM;

    if (!dispatchEnabled)
        return;

    // Record-time inputs for the push constants, the shadow sun steps in discrete intervals
    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
    auto& dayNightCycle = registry->ctx().get<ECS::Singletons::DayNightCycle>();
    const f32 shadowTimeOfDay = ECS::Systems::GetShadowTimeOfDay(dayNightCycle.GetTimeInSecondsF32());
    const vec3 lightDirection = ECS::Systems::UpdateAreaLights::GetLightDirection(shadowTimeOfDay);

    renderGraph->AddPass<Data>("Shadow Cascade Fit",
        [this, &resources](Data& data, Renderer::RenderGraphBuilder& builder)
        {
            using BufferUsage = Renderer::BufferPassUsage;

            data.depth = builder.Read(resources.depth, Renderer::PipelineType::COMPUTE);

            data.cameras = builder.Write(resources.cameras.GetBuffer(), BufferUsage::COMPUTE | BufferUsage::TRANSFER);
            builder.Read(_depthMinMaxBuffer, BufferUsage::COMPUTE);
            data.sdsmDataBuffer = builder.Write(_sdsmDataBuffer, BufferUsage::COMPUTE | BufferUsage::TRANSFER);
            data.cascadeBoundsBuffer = builder.Write(_cascadeBoundsBuffer, BufferUsage::TRANSFER | BufferUsage::COMPUTE);
            data.sdsmDataReadBackBuffer = builder.Write(_sdsmDataReadBackBuffer, BufferUsage::TRANSFER);
            data.cascadeCamerasReadBackBuffer = builder.Write(_cascadeCamerasReadBackBuffer, BufferUsage::TRANSFER);

            data.rangeSet = builder.Use(_cascadeFitRangeDescriptorSet);
            data.reduceSet = builder.Use(_cascadeXYReduceDescriptorSet);
            data.camerasSet = builder.Use(_cascadeFitCamerasDescriptorSet);

            return true; // Return true from setup to enable this pass, return false to disable it
        },
        [this, frameIndex, numCascades, lightDirection, cvarSystem](Data& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList)
        {
            GPU_SCOPED_PROFILER_ZONE(commandList, ShadowCascadeFit);

            struct CascadeFitConstants
            {
                vec4 lightDirection;
                u32 numCascades;
                f32 cascadeSplitLambda;
                f32 cascadeTextureSize;
                u32 stableShadows;
                f32 shadowMaxDistance;
                f32 quantizeStep;
                f32 shrinkDelay;
                f32 deltaTime;
                u32 useDepthBounds;
                f32 casterMargin;
                u32 useXYBounds;
                f32 xyMarginTexels;
            };

            CascadeFitConstants* constants = graphResources.FrameNew<CascadeFitConstants>();
            constants->lightDirection = vec4(lightDirection, 0.0f);
            constants->numCascades = numCascades;
            constants->cascadeSplitLambda = static_cast<f32>(*cvarSystem->GetFloatCVar(CVarCategory::Client | CVarCategory::Rendering, "shadowCascadeSplitLambda"));
            constants->cascadeTextureSize = static_cast<f32>(*cvarSystem->GetIntCVar(CVarCategory::Client | CVarCategory::Rendering, "shadowCascadeSize"));
            constants->stableShadows = *cvarSystem->GetIntCVar(CVarCategory::Client | CVarCategory::Rendering, "shadowStable") != 0;
            constants->shadowMaxDistance = static_cast<f32>(*cvarSystem->GetFloatCVar(CVarCategory::Client | CVarCategory::Rendering, "shadowMaxDistance"));
            constants->quantizeStep = CVAR_ShadowSDSMQuantizeStep.GetFloat();
            constants->shrinkDelay = CVAR_ShadowSDSMShrinkDelay.GetFloat();
            constants->deltaTime = _lastDeltaTime;
            constants->useDepthBounds = CVAR_ShadowSDSMUseDepthBounds.Get();
            constants->casterMargin = static_cast<f32>(*cvarSystem->GetFloatCVar(CVarCategory::Client | CVarCategory::Rendering, "shadowCasterMargin"));

            // The XY path presumes the stable up rule and depth bounds, and parity compares
            // against the legacy CPU math which it intentionally diverges from
            u32 useXYBounds = static_cast<u32>(Math::Max(CVAR_ShadowSDSMUseXYBounds.Get(), 0));
            if (CVAR_ShadowSDSMValidateParity.Get() || !constants->useDepthBounds || !constants->stableShadows)
            {
                useXYBounds = Math::Min(useXYBounds, 1u);
            }
            constants->useXYBounds = useXYBounds;
            constants->xyMarginTexels = CVAR_ShadowSDSMXYMarginTexels.GetFloat() + CVAR_ShadowNormalOffsetBias.GetFloat();

            // Reset the light-space bounds to the encoded sentinels
            commandList.FillBuffer(data.cascadeBoundsBuffer, 0, sizeof(u32) * 24, 0xFFFFFFFF);
            commandList.FillBuffer(data.cascadeBoundsBuffer, sizeof(u32) * 24, sizeof(u32) * 24, 0);
            commandList.BufferBarrier(data.cascadeBoundsBuffer, Renderer::BufferPassUsage::TRANSFER);

            // Range: reduced depth bounds -> working range + split distances
            commandList.BeginPipeline(_cascadeFitRangePipeline);
            commandList.PushConstant(constants, 0, sizeof(CascadeFitConstants));

            commandList.BindDescriptorSet(data.rangeSet, frameIndex);

            commandList.Dispatch(1, 1, 1);
            commandList.EndPipeline(_cascadeFitRangePipeline);

            commandList.BufferBarrier(data.sdsmDataBuffer, Renderer::BufferPassUsage::COMPUTE);

            // XY reduce: light-space AABB of the visible samples per cascade
            if (useXYBounds >= 1)
            {
                commandList.BeginPipeline(_cascadeXYReducePipeline);
                commandList.PushConstant(constants, 0, sizeof(CascadeFitConstants));

                data.reduceSet.Bind("_depth"_h, data.depth);
                commandList.BindDescriptorSet(data.reduceSet, frameIndex);

                uvec2 depthDimensions = graphResources.GetImageDimensions(data.depth);
                commandList.Dispatch((depthDimensions.x + 15) / 16, (depthDimensions.y + 15) / 16, 1);

                commandList.EndPipeline(_cascadeXYReducePipeline);

                commandList.BufferBarrier(data.cascadeBoundsBuffer, Renderer::BufferPassUsage::COMPUTE);
            }

            // Cameras: build the cascade cameras from the splits
            commandList.BeginPipeline(_cascadeFitCamerasPipeline);
            commandList.PushConstant(constants, 0, sizeof(CascadeFitConstants));

            commandList.BindDescriptorSet(data.camerasSet, frameIndex);

            commandList.Dispatch(1, 1, 1);
            commandList.EndPipeline(_cascadeFitCamerasPipeline);

            commandList.BufferBarrier(data.cameras, Renderer::BufferPassUsage::COMPUTE);
            commandList.BufferBarrier(data.sdsmDataBuffer, Renderer::BufferPassUsage::COMPUTE);

            // Readbacks for the debug tooling, element 0 of the cameras buffer is the main camera
            commandList.CopyBuffer(data.cascadeCamerasReadBackBuffer, 0, data.cameras, sizeof(Camera), sizeof(Camera) * numCascades);
            commandList.CopyBuffer(data.sdsmDataReadBackBuffer, 0, data.sdsmDataBuffer, 0, sizeof(f32) * SDSM_DATA_FLOAT_COUNT);
        });
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
    const bool enabled = CVAR_ShadowTechnique.Get() == 1 && CVAR_ShadowEnabled.Get() && !CVAR_SVSMFreeze.Get();
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
    if (CVAR_ShadowTechnique.Get() != 1 || !CVAR_ShadowEnabled.Get() || (debugClipmap < 0 && !showPool))
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

bool ShadowRenderer::GetEffectiveShadowRange(f32& outMinDistance, f32& outMaxDistance) const
{
    f32 usedMin = _sdsmDataReadBack[2];
    f32 usedMax = _sdsmDataReadBack[3];
    if (usedMax <= usedMin) // Not fitted yet
        return false;

    outMinDistance = usedMin;
    outMaxDistance = usedMax;
    return true;
}

bool ShadowRenderer::GetCascadeFittedBounds(u32 cascadeIndex, vec3& outExtents, bool& outValid) const
{
    if (CVAR_ShadowSDSMUseXYBounds.Get() < 1 || cascadeIndex >= Renderer::Settings::MAX_SHADOW_CASCADES)
        return false;

    // cascadeDiag lives after SDSMState (8) + splitDist (8) + splitDepth (8) + cascadeStable (32)
    const f32* diag = &_sdsmDataReadBack[56 + cascadeIndex * 4];
    outExtents = vec3(diag[0], diag[1], diag[2]);
    outValid = diag[3] > 0.5f;
    return true;
}

bool ShadowRenderer::GetDepthBoundsViewDistances(const RenderResources& resources, f32& outMinDistance, f32& outMaxDistance) const
{
    if (_depthMinMaxReadBack[1] == 0) // Sentinel, no valid depth samples reduced yet
        return false;

    const vec4& nearFar = resources.cameras[0].nearFar;
    f32 nearClip = nearFar.x;
    f32 farClip = nearFar.y;

    // Reversed Z linearization, depth 1 = near plane, depth 0 = far plane
    auto Linearize = [nearClip, farClip](f32 depth) { return (nearClip * farClip) / (nearClip + depth * (farClip - nearClip)); };

    outMinDistance = Linearize(glm::uintBitsToFloat(_depthMinMaxReadBack[1])); // Max depth bits = nearest sample
    outMaxDistance = Linearize(glm::uintBitsToFloat(_depthMinMaxReadBack[0])); // Min depth bits = farthest sample
    return true;
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

void ShadowRenderer::AddShadowPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex)
{
    struct ShadowPassData
    {
        Renderer::DepthImageMutableResource shadowDepthCascades[Renderer::Settings::MAX_SHADOW_CASCADES];
        Renderer::ImageResource svsmPagePool;
        Renderer::ImageResource svsmDynamicPagePool;

        Renderer::DescriptorSetResource lightDescriptorSet;
    };

    CVarSystem* cvarSystem = CVarSystem::Get();
    u32 numCascades = static_cast<u32>(*cvarSystem->GetIntCVar(CVarCategory::Client | CVarCategory::Rendering, "shadowCascadeNum"));
    numCascades = std::min(numCascades, static_cast<u32>(resources.shadowDepthCascades.size()));

    if (numCascades == 0)
        return;

    renderGraph->AddPass<ShadowPassData>("Shadow Pass",
        [=, &resources](ShadowPassData& data, Renderer::RenderGraphBuilder& builder)
        {
            // Under SVSM the cascade RTs are never rendered or sampled, skip the 4x4096 clears but
            // keep them bound so the material pass layout stays satisfied
            const Renderer::LoadMode cascadeLoadMode = CVAR_ShadowTechnique.Get() == 1 ? Renderer::LoadMode::LOAD : Renderer::LoadMode::CLEAR;
            for (u32 i = 0; i < numCascades; i++)
            {
                data.shadowDepthCascades[i] = builder.Write(resources.shadowDepthCascades[i], Renderer::PipelineType::GRAPHICS, cascadeLoadMode);
            }

            data.svsmPagePool = builder.Read(GetSVSMPagePoolOrPlaceholder(), Renderer::PipelineType::COMPUTE);
            data.svsmDynamicPagePool = builder.Read(GetSVSMDynamicPagePoolOrPlaceholder(), Renderer::PipelineType::COMPUTE);

            data.lightDescriptorSet = builder.Use(resources.lightDescriptorSet);

            return true; // Return true from setup to enable this pass, return false to disable it
        },
        [=](ShadowPassData& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList)
        {
            Renderer::DepthImageMutableResource cascadeDepthResource;
            for (u32 i = 0; i < Renderer::Settings::MAX_SHADOW_CASCADES; i++)
            {
                if (i < numCascades)
                {
                    cascadeDepthResource = data.shadowDepthCascades[i];
                }

                data.lightDescriptorSet.BindArray("_shadowCascadeRTs", cascadeDepthResource, i);
            }

            // Every LIGHT set consumer needs the SVSM pool bindings valid, real pools or placeholder
            data.lightDescriptorSet.Bind("_svsmPagePool"_h, data.svsmPagePool);
            data.lightDescriptorSet.Bind("_svsmDynamicPagePool"_h, data.svsmDynamicPagePool);
        });
}

void ShadowRenderer::CreatePermanentResources(RenderResources& resources)
{
    Renderer::SamplerDesc samplerDesc;
    samplerDesc.enabled = true;
    samplerDesc.filter = Renderer::SamplerFilter::MIN_MAG_MIP_LINEAR;
    samplerDesc.addressU = Renderer::TextureAddressMode::CLAMP;
    samplerDesc.addressV = Renderer::TextureAddressMode::CLAMP;
    samplerDesc.addressW = Renderer::TextureAddressMode::CLAMP;
    samplerDesc.shaderVisibility = Renderer::ShaderVisibility::PIXEL;
    samplerDesc.comparisonEnabled = true;
    samplerDesc.comparisonFunc = Renderer::ComparisonFunc::GREATER;

    _shadowCmpSampler = _renderer->CreateSampler(samplerDesc);
    resources.lightDescriptorSet.Bind("_shadowCmpSampler"_h, _shadowCmpSampler);

    samplerDesc.filter = Renderer::SamplerFilter::MIN_MAG_MIP_POINT;
    samplerDesc.addressU = Renderer::TextureAddressMode::CLAMP;
    samplerDesc.addressV = Renderer::TextureAddressMode::CLAMP;
    samplerDesc.comparisonEnabled = false;

    _shadowPointClampSampler = _renderer->CreateSampler(samplerDesc);
    resources.lightDescriptorSet.Bind("_shadowPointClampSampler"_h, _shadowPointClampSampler);

    // Depth min/max reduction for SDSM cascade fitting
    {
        Renderer::ComputePipelineDesc pipelineDesc;
        pipelineDesc.debugName = "Shadow Depth MinMax";

        Renderer::ComputeShaderDesc shaderDesc;
        shaderDesc.shaderEntry = _gameRenderer->GetShaderEntry("Shadows/DepthMinMax.cs"_h, "Shadows/DepthMinMax.cs");
        pipelineDesc.computeShader = _renderer->LoadShader(shaderDesc);

        _depthMinMaxPipeline = _renderer->CreatePipeline(pipelineDesc);

        _depthMinMaxDescriptorSet.RegisterPipeline(_renderer, _depthMinMaxPipeline);
        _depthMinMaxDescriptorSet.Init(_renderer);

        Renderer::BufferDesc bufferDesc;
        bufferDesc.name = "ShadowDepthMinMax";
        bufferDesc.size = sizeof(u32) * 2;
        bufferDesc.usage = Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION | Renderer::BufferUsage::TRANSFER_SOURCE;

        _depthMinMaxBuffer = _renderer->CreateAndFillBuffer(_depthMinMaxBuffer, bufferDesc, [](void* mappedMemory, size_t size)
        {
            u32* values = static_cast<u32*>(mappedMemory);
            values[0] = 0xFFFFFFFF; // Min depth bits sentinel
            values[1] = 0;          // Max depth bits sentinel
        });
        _depthMinMaxDescriptorSet.Bind("_depthMinMax"_h, _depthMinMaxBuffer);

        bufferDesc.name = "ShadowDepthMinMaxReadBack";
        bufferDesc.usage = Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION;
        bufferDesc.cpuAccess = Renderer::BufferCPUAccess::ReadOnly;
        _depthMinMaxReadBackBuffer = _renderer->CreateBuffer(_depthMinMaxReadBackBuffer, bufferDesc);
    }

    // SDSM cascade fitting
    {
        Renderer::ComputePipelineDesc pipelineDesc;
        pipelineDesc.debugName = "Shadow Cascade Fit Range";

        Renderer::ComputeShaderDesc shaderDesc;
        shaderDesc.shaderEntry = _gameRenderer->GetShaderEntry("Shadows/CascadeFitRange.cs"_h, "Shadows/CascadeFitRange.cs");
        pipelineDesc.computeShader = _renderer->LoadShader(shaderDesc);

        _cascadeFitRangePipeline = _renderer->CreatePipeline(pipelineDesc);

        _cascadeFitRangeDescriptorSet.RegisterPipeline(_renderer, _cascadeFitRangePipeline);
        _cascadeFitRangeDescriptorSet.Init(_renderer);

        pipelineDesc.debugName = "Shadow Cascade XY Reduce";
        shaderDesc.shaderEntry = _gameRenderer->GetShaderEntry("Shadows/CascadeXYReduce.cs"_h, "Shadows/CascadeXYReduce.cs");
        pipelineDesc.computeShader = _renderer->LoadShader(shaderDesc);

        _cascadeXYReducePipeline = _renderer->CreatePipeline(pipelineDesc);

        _cascadeXYReduceDescriptorSet.RegisterPipeline(_renderer, _cascadeXYReducePipeline);
        _cascadeXYReduceDescriptorSet.Init(_renderer);

        pipelineDesc.debugName = "Shadow Cascade Fit Cameras";
        shaderDesc.shaderEntry = _gameRenderer->GetShaderEntry("Shadows/CascadeFitCameras.cs"_h, "Shadows/CascadeFitCameras.cs");
        pipelineDesc.computeShader = _renderer->LoadShader(shaderDesc);

        _cascadeFitCamerasPipeline = _renderer->CreatePipeline(pipelineDesc);

        _cascadeFitCamerasDescriptorSet.RegisterPipeline(_renderer, _cascadeFitCamerasPipeline);
        _cascadeFitCamerasDescriptorSet.Init(_renderer);

        Renderer::BufferDesc bufferDesc;
        bufferDesc.name = "ShadowSDSMData";
        bufferDesc.size = sizeof(f32) * SDSM_DATA_FLOAT_COUNT;
        bufferDesc.usage = Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION | Renderer::BufferUsage::TRANSFER_SOURCE;

        _sdsmDataBuffer = _renderer->CreateAndFillBuffer(_sdsmDataBuffer, bufferDesc, [](void* mappedMemory, size_t size)
        {
            memset(mappedMemory, 0, size); // smoothedMax <= smoothedMin marks the state uninitialized
        });

        Renderer::BufferDesc boundsDesc;
        boundsDesc.name = "ShadowCascadeBounds";
        boundsDesc.size = sizeof(u32) * 48; // Encoded light-space mins [0..23] and maxs [24..47], 3 axes per cascade
        boundsDesc.usage = Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION;
        _cascadeBoundsBuffer = _renderer->CreateAndFillBuffer(_cascadeBoundsBuffer, boundsDesc, [](void* mappedMemory, size_t size)
        {
            u32* values = static_cast<u32*>(mappedMemory);
            for (u32 i = 0; i < 24; i++) values[i] = 0xFFFFFFFF;
            for (u32 i = 24; i < 48; i++) values[i] = 0;
        });

        _cascadeFitRangeDescriptorSet.Bind("_sdsmData"_h, _sdsmDataBuffer);
        _cascadeFitRangeDescriptorSet.Bind("_depthMinMax"_h, _depthMinMaxBuffer);
        _cascadeXYReduceDescriptorSet.Bind("_sdsmData"_h, _sdsmDataBuffer);
        _cascadeXYReduceDescriptorSet.Bind("_cascadeBounds"_h, _cascadeBoundsBuffer);
        _cascadeFitCamerasDescriptorSet.Bind("_sdsmData"_h, _sdsmDataBuffer);
        _cascadeFitCamerasDescriptorSet.Bind("_cascadeBounds"_h, _cascadeBoundsBuffer);
        // _srcCameras / _rwCameras / _depth are bound per frame in AddCascadeFitPass, those resources can be recreated

        bufferDesc.name = "ShadowSDSMDataReadBack";
        bufferDesc.usage = Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION;
        bufferDesc.cpuAccess = Renderer::BufferCPUAccess::ReadOnly;
        _sdsmDataReadBackBuffer = _renderer->CreateBuffer(_sdsmDataReadBackBuffer, bufferDesc);

        bufferDesc.name = "ShadowCascadeCamerasReadBack";
        bufferDesc.size = sizeof(Camera) * Renderer::Settings::MAX_SHADOW_CASCADES;
        bufferDesc.usage = Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION;
        bufferDesc.cpuAccess = Renderer::BufferCPUAccess::ReadOnly;
        _cascadeCamerasReadBackBuffer = _renderer->CreateBuffer(_cascadeCamerasReadBackBuffer, bufferDesc);
    }

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

    _cascadeFitRangeDescriptorSet.Bind("_srcCameras"_h, camerasBuffer);
    _cascadeXYReduceDescriptorSet.Bind("_srcCameras"_h, camerasBuffer);
    _cascadeFitCamerasDescriptorSet.Bind("_rwCameras"_h, camerasBuffer);
    _svsmPrepareDescriptorSet.Bind("_srcCameras"_h, camerasBuffer);
    _svsmPageMarkDescriptorSet.Bind("_srcCameras"_h, camerasBuffer);
    _svsmFinalizeDescriptorSet.Bind("_rwCameras"_h, camerasBuffer);
}