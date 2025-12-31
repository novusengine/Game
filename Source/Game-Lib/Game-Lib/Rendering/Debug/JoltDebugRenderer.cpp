#include "JoltDebugRenderer.h"

#include "Game-Lib/ECS/Singletons/JoltState.h"
#include "Game-Lib/Rendering/CullUtils.h"
#include "Game-Lib/Rendering/Debug/DebugRenderer.h"
#include "Game-Lib/Rendering/RenderUtils.h"
#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Rendering/RenderResources.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Renderer/Renderer.h>
#include <Renderer/RenderGraph.h>
#include <Renderer/Descriptors/ImageDesc.h>

#include <Base/CVarSystem/CVarSystem.h>

#include <entt/entt.hpp>
#include <Jolt/Jolt.h>

AutoCVar_ShowFlag CVAR_JoltDebugRender(CVarCategory::Client | CVarCategory::Rendering, "joltEnabled", "Render collision meshes as seen by Jolt", ShowFlag::DISABLED);

//AutoCVar_Int CVAR_JoltDebugCullingEnabled("joltDebugRenderer.culling", "enable jolt debug culling", 1, CVarFlags::EditCheckbox);
AutoCVar_Int CVAR_JoltDebugOcclusionCullingEnabled(CVarCategory::Client | CVarCategory::Rendering, "joltOcclusionCulling", "enable jolt debug occlusion culling", 1, CVarFlags::EditCheckbox);

AutoCVar_Int CVAR_JoltDebugDisableTwoStepCulling(CVarCategory::Client | CVarCategory::Rendering, "joltDisableTwoStepCulling", "disable two step culling and force all drawcalls into the geometry pass", 0, CVarFlags::EditCheckbox);

AutoCVar_Int CVAR_JoltDebugDrawOccluders(CVarCategory::Client | CVarCategory::Rendering, "joltDrawOccluders", "enable the draw command for occluders, the culling and everything else is unaffected", 1, CVarFlags::EditCheckbox);
AutoCVar_Int CVAR_JoltDebugDrawGeometry(CVarCategory::Client | CVarCategory::Rendering, "joltDrawGeometry", "enable the draw command for geometry, the culling and everything else is unaffected", 1, CVarFlags::EditCheckbox);

AutoCVar_Int CVAR_JoltDebugDrawAABBs(CVarCategory::Client | CVarCategory::Rendering, "joltDrawAABBs", "if enabled, the culling pass will debug draw AABBs", 0, CVarFlags::EditCheckbox);

AutoCVar_Int CVAR_JoltDebugValidateTransfers(CVarCategory::Client | CVarCategory::Rendering, "joltValidateGPUVectors", "if enabled ON START we will validate GPUVector uploads", 0, CVarFlags::EditCheckbox);

JoltDebugRenderer::JoltDebugRenderer(Renderer::Renderer* renderer, GameRenderer* gameRenderer, ::DebugRenderer* debugRenderer)
    : CulledRenderer(renderer, gameRenderer, debugRenderer)
    , _gameRenderer(gameRenderer)
{
#ifdef JPH_DEBUG_RENDERER
    // Initialize base class
    DebugRenderer::Initialize();
#endif

    CreatePermanentResources();

    if (CVAR_JoltDebugValidateTransfers.Get())
    {
        _vertices.SetValidation(true);
        _indices.SetValidation(true);
        _instances.SetValidation(true);

        _cullingDatas.SetValidation(true);
        _indexedCullingResources.SetValidation(true);
        _cullingResources.SetValidation(true);
    }
}

void JoltDebugRenderer::Update(f32 deltaTime)
{
    ZoneScoped;

    if (CVAR_JoltDebugRender.Get() == ShowFlag::DISABLED)
    {
        _indexedCullingResources.ResetCullingStats();
        _cullingResources.ResetCullingStats();
        return;
    }

#ifdef JPH_DEBUG_RENDERER
    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
    entt::registry::context& ctx = registry->ctx();
    auto& joltState = ctx.get<ECS::Singletons::JoltState>();

    JPH::BodyManager::DrawSettings drawSettings;
    //drawSettings.mDrawBoundingBox = true;
    drawSettings.mDrawShape = true;

    joltState.physicsSystem.DrawBodies(drawSettings, this);
#endif

    Compact();

    const bool cullingEnabled = true;//CVAR_JoltDebugCullingEnabled.Get();
    _indexedCullingResources.Update(deltaTime, cullingEnabled);
    _cullingResources.Update(deltaTime, cullingEnabled);

    SyncToGPU();
}

void JoltDebugRenderer::Clear()
{
    _vertices.Clear();
    _indices.Clear();
    _instances.Clear();

    _indexedDrawManifests.clear();
    _drawManifests.clear();

    _cullingDatas.Clear();
    _indexedCullingResources.Clear();
    _cullingResources.Clear();

    SyncToGPU();
}

void JoltDebugRenderer::AddOccluderPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex)
{
    if (CVAR_JoltDebugRender.Get() == ShowFlag::DISABLED)
        return;

    //if (!CVAR_JoltDebugCullingEnabled.Get())
    //    return;

    u32 numCascades = 0; // Debug meshes should never cast shadows

    struct Data
    {
        Renderer::ImageMutableResource visibilityBuffer;
        Renderer::DepthImageMutableResource depth;

        Renderer::BufferMutableResource culledDrawCallsBuffer;
        Renderer::BufferMutableResource culledDrawCallCountBuffer;

        Renderer::BufferMutableResource culledDrawCallsBitMaskBuffer;
        Renderer::BufferMutableResource prevCulledDrawCallsBitMaskBuffer;

        Renderer::BufferMutableResource culledInstanceCountsBuffer;

        Renderer::BufferMutableResource drawCountBuffer;
        Renderer::BufferMutableResource triangleCountBuffer;
        Renderer::BufferMutableResource drawCountReadBackBuffer;
        Renderer::BufferMutableResource triangleCountReadBackBuffer;

        Renderer::DescriptorSetResource globalSet;
        Renderer::DescriptorSetResource occluderFillSet;
        Renderer::DescriptorSetResource createIndirectDescriptorSet;
        Renderer::DescriptorSetResource drawSet;
    };

    if (_indexedCullingResources.GetNumInstances() > 0)
    {
        renderGraph->AddPass<Data>("Jolt Debug Occluders (I)",
            [this, &resources, frameIndex](Data& data, Renderer::RenderGraphBuilder& builder)
            {
                using BufferUsage = Renderer::BufferPassUsage;

                data.visibilityBuffer = builder.Write(resources.visibilityBuffer, Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::LOAD);
                data.depth = builder.Write(resources.depth, Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::LOAD);

                builder.Read(resources.cameras.GetBuffer(), BufferUsage::GRAPHICS | BufferUsage::COMPUTE);
                builder.Read(_vertices.GetBuffer(), BufferUsage::GRAPHICS);
                builder.Read(_indices.GetBuffer(), BufferUsage::GRAPHICS);
                builder.Read(_instances.GetBuffer(), BufferUsage::GRAPHICS | BufferUsage::COMPUTE);

                OccluderPassSetup(data, builder, &_indexedCullingResources, frameIndex);
                data.culledDrawCallCountBuffer = builder.Write(_indexedCullingResources.GetCulledDrawCallCountBuffer(), BufferUsage::TRANSFER | BufferUsage::COMPUTE | BufferUsage::GRAPHICS);
                data.culledInstanceCountsBuffer = builder.Write(_indexedCullingResources.GetCulledInstanceCountsBuffer(), BufferUsage::TRANSFER | BufferUsage::COMPUTE);

                builder.Read(_indexedCullingResources.GetDrawCallDatas().GetBuffer(), BufferUsage::GRAPHICS | BufferUsage::COMPUTE);

                data.globalSet = builder.Use(resources.globalDescriptorSet);

                return true; // Return true from setup to enable this pass, return false to disable it
            },
            [this, &resources, frameIndex](Data& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList) // Execute
            {
                GPU_SCOPED_PROFILER_ZONE(commandList, JoltDebugOccluders);

                CulledRenderer::OccluderPassParams params;
                params.passName = "";
                params.graphResources = &graphResources;
                params.commandList = &commandList;
                params.cullingResources = &_indexedCullingResources;

                params.frameIndex = frameIndex;
                params.rt0 = data.visibilityBuffer;
                params.depth[0] = data.depth;

                params.culledDrawCallsBuffer = data.culledDrawCallsBuffer;
                params.culledDrawCallCountBuffer = data.culledDrawCallCountBuffer;
                params.culledDrawCallsBitMaskBuffer = data.culledDrawCallsBitMaskBuffer;
                params.prevCulledDrawCallsBitMaskBuffer = data.prevCulledDrawCallsBitMaskBuffer;
                params.culledInstanceCountsBuffer = data.culledInstanceCountsBuffer;

                params.drawCountBuffer = data.drawCountBuffer;
                params.triangleCountBuffer = data.triangleCountBuffer;
                params.drawCountReadBackBuffer = data.drawCountReadBackBuffer;
                params.triangleCountReadBackBuffer = data.triangleCountReadBackBuffer;

                params.globalDescriptorSet = data.globalSet;
                params.occluderFillDescriptorSet = data.occluderFillSet;
                params.createIndirectDescriptorSet = data.createIndirectDescriptorSet;
                params.drawDescriptorSet = data.drawSet;

                params.drawCallback = [&](DrawParams& drawParams)
                {
                    drawParams.descriptorSets = {
                        &data.globalSet,
                        &data.drawSet
                    };
                    Draw(resources, frameIndex, graphResources, commandList, drawParams);
                };

                params.baseInstanceLookupOffset = offsetof(DrawCallData, DrawCallData::baseInstanceLookupOffset);
                params.drawCallDataSize = sizeof(DrawCallData);

                params.enableDrawing = CVAR_JoltDebugDrawOccluders.Get();
                params.disableTwoStepCulling = CVAR_JoltDebugDisableTwoStepCulling.Get();

                OccluderPass(params);
            });
    }

    if (_cullingResources.GetNumInstances() > 0)
    {
        renderGraph->AddPass<Data>("Jolt Debug Occluders",
            [this, &resources, frameIndex](Data& data, Renderer::RenderGraphBuilder& builder)
            {
                using BufferUsage = Renderer::BufferPassUsage;

                data.visibilityBuffer = builder.Write(resources.visibilityBuffer, Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::LOAD);
                data.depth = builder.Write(resources.depth, Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::LOAD);

                builder.Read(resources.cameras.GetBuffer(), BufferUsage::GRAPHICS | BufferUsage::COMPUTE);
                builder.Read(_vertices.GetBuffer(), BufferUsage::GRAPHICS);
                builder.Read(_indices.GetBuffer(), BufferUsage::GRAPHICS);
                builder.Read(_instances.GetBuffer(), BufferUsage::GRAPHICS | BufferUsage::COMPUTE);

                OccluderPassSetup(data, builder, &_cullingResources, frameIndex);
                data.culledDrawCallCountBuffer = builder.Write(_cullingResources.GetCulledDrawCallCountBuffer(), BufferUsage::TRANSFER | BufferUsage::COMPUTE | BufferUsage::GRAPHICS);
                data.culledInstanceCountsBuffer = builder.Write(_cullingResources.GetCulledInstanceCountsBuffer(), BufferUsage::TRANSFER | BufferUsage::COMPUTE);
                
                builder.Read(_cullingResources.GetDrawCallDatas().GetBuffer(), BufferUsage::GRAPHICS | BufferUsage::COMPUTE);

                data.globalSet = builder.Use(resources.globalDescriptorSet);

                return true; // Return true from setup to enable this pass, return false to disable it
            },
            [this, &resources, frameIndex](Data& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList) // Execute
            {
                GPU_SCOPED_PROFILER_ZONE(commandList, JoltDebugOccluders);

                CulledRenderer::OccluderPassParams params;
                params.passName = "";
                params.graphResources = &graphResources;
                params.commandList = &commandList;
                params.cullingResources = &_cullingResources;

                params.frameIndex = frameIndex;
                params.rt0 = data.visibilityBuffer;
                params.depth[0] = data.depth;

                params.culledDrawCallsBuffer = data.culledDrawCallsBuffer;
                params.culledDrawCallCountBuffer = data.culledDrawCallCountBuffer;
                params.culledDrawCallsBitMaskBuffer = data.culledDrawCallsBitMaskBuffer;
                params.prevCulledDrawCallsBitMaskBuffer = data.prevCulledDrawCallsBitMaskBuffer;
                params.culledInstanceCountsBuffer = data.culledInstanceCountsBuffer;

                params.drawCountBuffer = data.drawCountBuffer;
                params.triangleCountBuffer = data.triangleCountBuffer;
                params.drawCountReadBackBuffer = data.drawCountReadBackBuffer;
                params.triangleCountReadBackBuffer = data.triangleCountReadBackBuffer;

                params.globalDescriptorSet = data.globalSet;
                params.occluderFillDescriptorSet = data.occluderFillSet;
                params.createIndirectDescriptorSet = data.createIndirectDescriptorSet;
                params.drawDescriptorSet = data.drawSet;

                params.drawCallback = [&](DrawParams& drawParams)
                {
                    drawParams.descriptorSets = {
                        &data.globalSet,
                        &data.drawSet
                    };
                    Draw(resources, frameIndex, graphResources, commandList, drawParams);
                };

                params.baseInstanceLookupOffset = offsetof(DrawCallData, DrawCallData::baseInstanceLookupOffset);
                params.drawCallDataSize = sizeof(DrawCallData);

                params.enableDrawing = CVAR_JoltDebugDrawOccluders.Get();
                params.disableTwoStepCulling = CVAR_JoltDebugDisableTwoStepCulling.Get();

                OccluderPass(params);
            });
    }
}

void JoltDebugRenderer::AddCullingPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex)
{
    if (CVAR_JoltDebugRender.Get() == ShowFlag::DISABLED)
        return;

    //if (!CVAR_JoltDebugCullingEnabled.Get())
    //    return;

    u32 numCascades = 0; // Debug meshes should never cast shadows

    struct Data
    {
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

        Renderer::DescriptorSetResource debugSet;
        Renderer::DescriptorSetResource globalSet;
        Renderer::DescriptorSetResource cullingSet;
        Renderer::DescriptorSetResource createIndirectAfterCullSet;
    };

    if (_indexedCullingResources.GetDrawCalls().Count() > 0)
    {
        renderGraph->AddPass<Data>("Jolt Debug Culling (I)",
            [this, &resources, frameIndex](Data& data, Renderer::RenderGraphBuilder& builder)
            {
                using BufferUsage = Renderer::BufferPassUsage;

                data.depthPyramid = builder.Read(resources.depthPyramid, Renderer::PipelineType::COMPUTE);

                builder.Read(resources.cameras.GetBuffer(), BufferUsage::COMPUTE);
                builder.Read(_cullingDatas.GetBuffer(), BufferUsage::COMPUTE);
                builder.Read(_instances.GetBuffer(), BufferUsage::COMPUTE);

                CullingPassSetup(data, builder, &_indexedCullingResources, frameIndex);
                builder.Read(_indexedCullingResources.GetDrawCallDatas().GetBuffer(), BufferUsage::COMPUTE);

                data.debugSet = builder.Use(_debugRenderer->GetDebugDescriptorSet());
                data.globalSet = builder.Use(resources.globalDescriptorSet);

                _debugRenderer->RegisterCullingPassBufferUsage(builder);

                return true; // Return true from setup to enable this pass, return false to disable it
            },
            [this, frameIndex](Data& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList) // Execute
            {
                GPU_SCOPED_PROFILER_ZONE(commandList, JoltDebugOccluders);

                CulledRenderer::CullingPassParams params;
                params.passName = "";
                params.graphResources = &graphResources;
                params.commandList = &commandList;
                params.cullingResources = &_indexedCullingResources;
                params.frameIndex = frameIndex;

                params.depthPyramid = data.depthPyramid;

                params.prevCulledDrawCallsBitMask = data.prevCulledDrawCallsBitMask;

                params.currentCulledDrawCallsBitMask = data.currentCulledDrawCallsBitMask;
                params.culledInstanceCountsBuffer = data.culledInstanceCountsBuffer;
                params.culledDrawCallsBuffer = data.culledDrawCallsBuffer;
                params.culledDrawCallCountBuffer = data.culledDrawCallCountBuffer;

                params.drawCountBuffer = data.drawCountBuffer;
                params.triangleCountBuffer = data.triangleCountBuffer;
                params.drawCountReadBackBuffer = data.drawCountReadBackBuffer;
                params.triangleCountReadBackBuffer = data.triangleCountReadBackBuffer;

                params.debugDescriptorSet = data.debugSet;
                params.globalDescriptorSet = data.globalSet;
                params.cullingDescriptorSet = data.cullingSet;
                params.createIndirectAfterCullSet = data.createIndirectAfterCullSet;

                params.numCascades = 0;
                params.occlusionCull = CVAR_JoltDebugOcclusionCullingEnabled.Get();

                params.cullingDataIsWorldspace = false;
                params.debugDrawColliders = CVAR_JoltDebugDrawAABBs.Get();

                params.drawCallDataSize = sizeof(DrawCallData);

                CullingPass(params);
            });
    }

    if (_cullingResources.GetDrawCalls().Count() > 0)
    {
        renderGraph->AddPass<Data>("Jolt Debug Culling",
            [this, &resources, frameIndex](Data& data, Renderer::RenderGraphBuilder& builder)
            {
                using BufferUsage = Renderer::BufferPassUsage;

                data.depthPyramid = builder.Read(resources.depthPyramid, Renderer::PipelineType::COMPUTE);

                builder.Read(resources.cameras.GetBuffer(), BufferUsage::COMPUTE);
                builder.Read(_cullingDatas.GetBuffer(), BufferUsage::COMPUTE);
                builder.Read(_instances.GetBuffer(), BufferUsage::COMPUTE);

                CullingPassSetup(data, builder, &_cullingResources, frameIndex);
                builder.Read(_cullingResources.GetDrawCallDatas().GetBuffer(), BufferUsage::COMPUTE);

                data.debugSet = builder.Use(_debugRenderer->GetDebugDescriptorSet());
                data.globalSet = builder.Use(resources.globalDescriptorSet);

                _debugRenderer->RegisterCullingPassBufferUsage(builder);

                return true; // Return true from setup to enable this pass, return false to disable it
            },
            [this, frameIndex](Data& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList) // Execute
            {
                GPU_SCOPED_PROFILER_ZONE(commandList, JoltDebugOccluders);

                CulledRenderer::CullingPassParams params;
                params.passName = "";
                params.graphResources = &graphResources;
                params.commandList = &commandList;
                params.cullingResources = &_cullingResources;
                params.frameIndex = frameIndex;

                params.depthPyramid = data.depthPyramid;

                params.prevCulledDrawCallsBitMask = data.prevCulledDrawCallsBitMask;

                params.currentCulledDrawCallsBitMask = data.currentCulledDrawCallsBitMask;
                params.culledInstanceCountsBuffer = data.culledInstanceCountsBuffer;
                params.culledDrawCallsBuffer = data.culledDrawCallsBuffer;
                params.culledDrawCallCountBuffer = data.culledDrawCallCountBuffer;

                params.drawCountBuffer = data.drawCountBuffer;
                params.triangleCountBuffer = data.triangleCountBuffer;
                params.drawCountReadBackBuffer = data.drawCountReadBackBuffer;
                params.triangleCountReadBackBuffer = data.triangleCountReadBackBuffer;

                params.debugDescriptorSet = data.debugSet;
                params.globalDescriptorSet = data.globalSet;
                params.cullingDescriptorSet = data.cullingSet;

                params.numCascades = 0;
                params.occlusionCull = CVAR_JoltDebugOcclusionCullingEnabled.Get();

                params.cullingDataIsWorldspace = false;
                params.debugDrawColliders = CVAR_JoltDebugDrawAABBs.Get();

                params.drawCallDataSize = sizeof(DrawCallData);

                CullingPass(params);
            });
    }
}

void JoltDebugRenderer::AddGeometryPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex)
{
    if (CVAR_JoltDebugRender.Get() == ShowFlag::DISABLED)
        return;

    const bool cullingEnabled = true;//CVAR_JoltDebugCullingEnabled.Get(); // TODO This will crash if it's false

    struct Data
    {
        Renderer::ImageMutableResource visibilityBuffer;
        Renderer::DepthImageMutableResource depth;

        Renderer::BufferMutableResource drawCallsBuffer;
        Renderer::BufferMutableResource culledDrawCallsBuffer;
        Renderer::BufferMutableResource culledDrawCallCountBuffer;
        Renderer::BufferMutableResource culledDrawCallsBitMaskBuffer;
        Renderer::BufferMutableResource prevCulledDrawCallsBitMaskBuffer;

        Renderer::BufferMutableResource drawCountBuffer;
        Renderer::BufferMutableResource triangleCountBuffer;
        Renderer::BufferMutableResource drawCountReadBackBuffer;
        Renderer::BufferMutableResource triangleCountReadBackBuffer;

        Renderer::DescriptorSetResource globalSet;
        Renderer::DescriptorSetResource fillSet;
        Renderer::DescriptorSetResource drawSet;
    };

    if (_indexedCullingResources.GetDrawCalls().Count() > 0)
    {
        renderGraph->AddPass<Data>("Jolt Debug Geometry (I)",
            [this, &resources, frameIndex](Data& data, Renderer::RenderGraphBuilder& builder)
            {
                using BufferUsage = Renderer::BufferPassUsage;

                data.visibilityBuffer = builder.Write(resources.visibilityBuffer, Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::LOAD);
                data.depth = builder.Write(resources.depth, Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::LOAD);

                builder.Read(resources.cameras.GetBuffer(), BufferUsage::GRAPHICS);

                builder.Read(_vertices.GetBuffer(), BufferUsage::GRAPHICS);
                builder.Read(_indices.GetBuffer(), BufferUsage::GRAPHICS);
                builder.Read(_instances.GetBuffer(), BufferUsage::GRAPHICS);
                
                GeometryPassSetup(data, builder, &_indexedCullingResources, frameIndex);
                builder.Read(_indexedCullingResources.GetDrawCallDatas().GetBuffer(), BufferUsage::GRAPHICS);

                data.globalSet = builder.Use(resources.globalDescriptorSet);

                return true; // Return true from setup to enable this pass, return false to disable it
            },
            [this, &resources, frameIndex, cullingEnabled](Data& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList)
            {
                GPU_SCOPED_PROFILER_ZONE(commandList, JoltDebugGeometry);

                CulledRenderer::GeometryPassParams params;
                params.passName = "";
                params.graphResources = &graphResources;
                params.commandList = &commandList;
                params.cullingResources = &_indexedCullingResources;

                params.frameIndex = frameIndex;
                params.rt0 = data.visibilityBuffer;
                params.depth[0] = data.depth;

                params.drawCallsBuffer = data.drawCallsBuffer;
                params.culledDrawCallsBuffer = data.culledDrawCallsBuffer;
                params.culledDrawCallCountBuffer = data.culledDrawCallCountBuffer;
                params.culledDrawCallsBitMaskBuffer = data.culledDrawCallsBitMaskBuffer;
                params.prevCulledDrawCallsBitMaskBuffer = data.prevCulledDrawCallsBitMaskBuffer;

                params.drawCountBuffer = data.drawCountBuffer;
                params.triangleCountBuffer = data.triangleCountBuffer;
                params.drawCountReadBackBuffer = data.drawCountReadBackBuffer;
                params.triangleCountReadBackBuffer = data.triangleCountReadBackBuffer;

                params.globalDescriptorSet = data.globalSet;
                params.drawDescriptorSet = data.drawSet;

                params.drawCallback = [&](DrawParams& drawParams)
                {
                    drawParams.descriptorSets = {
                        &data.globalSet,
                        &data.drawSet
                    };
                    Draw(resources, frameIndex, graphResources, commandList, drawParams);
                };

                params.enableDrawing = CVAR_JoltDebugDrawGeometry.Get();
                params.cullingEnabled = cullingEnabled;
                params.numCascades = 0;

                GeometryPass(params);

                // Reset counts
                {
                    const Renderer::GPUVector<Renderer::IndexedIndirectDraw>& draws = _indexedCullingResources.GetDrawCalls();
                    for (u32 i = 0; i < draws.Count(); i++)
                    {
                        Renderer::IndexedIndirectDraw& draw = draws[i];
                        draw.instanceCount = 0;
                    }
                }

                for (DrawManifest& manifest : _indexedDrawManifests)
                {
                    manifest.instanceIDs.clear();
                }
            });
    }

    if (_cullingResources.GetDrawCalls().Count() > 0)
    {
        renderGraph->AddPass<Data>("Jolt Debug Geometry",
            [this, &resources, frameIndex](Data& data, Renderer::RenderGraphBuilder& builder)
            {
                using BufferUsage = Renderer::BufferPassUsage;

                data.visibilityBuffer = builder.Write(resources.visibilityBuffer, Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::LOAD);
                data.depth = builder.Write(resources.depth, Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::LOAD);

                builder.Read(resources.cameras.GetBuffer(), BufferUsage::GRAPHICS);

                builder.Read(_vertices.GetBuffer(), BufferUsage::GRAPHICS);
                builder.Read(_indices.GetBuffer(), BufferUsage::GRAPHICS);
                builder.Read(_instances.GetBuffer(), BufferUsage::GRAPHICS);

                GeometryPassSetup(data, builder, &_cullingResources, frameIndex);
                builder.Read(_cullingResources.GetDrawCallDatas().GetBuffer(), BufferUsage::GRAPHICS);

                data.globalSet = builder.Use(resources.globalDescriptorSet);

                return true; // Return true from setup to enable this pass, return false to disable it
            },
            [this, &resources, frameIndex, cullingEnabled](Data& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList)
            {
                GPU_SCOPED_PROFILER_ZONE(commandList, JoltDebugGeometry);

                CulledRenderer::GeometryPassParams params;
                params.passName = "";
                params.graphResources = &graphResources;
                params.commandList = &commandList;
                params.cullingResources = &_cullingResources;

                params.frameIndex = frameIndex;
                params.rt0 = data.visibilityBuffer;
                params.depth[0] = data.depth;

                params.culledDrawCallsBuffer = data.culledDrawCallsBuffer;
                params.culledDrawCallCountBuffer = data.culledDrawCallCountBuffer;
                params.culledDrawCallsBitMaskBuffer = data.culledDrawCallsBitMaskBuffer;
                params.prevCulledDrawCallsBitMaskBuffer = data.prevCulledDrawCallsBitMaskBuffer;

                params.drawCountBuffer = data.drawCountBuffer;
                params.triangleCountBuffer = data.triangleCountBuffer;
                params.drawCountReadBackBuffer = data.drawCountReadBackBuffer;
                params.triangleCountReadBackBuffer = data.triangleCountReadBackBuffer;

                params.globalDescriptorSet = data.globalSet;
                params.drawDescriptorSet = data.drawSet;

                params.drawCallback = [&](DrawParams& drawParams)
                {
                    drawParams.descriptorSets = {
                        &data.globalSet,
                        &data.drawSet
                    };
                    Draw(resources, frameIndex, graphResources, commandList, drawParams);
                };

                params.enableDrawing = CVAR_JoltDebugDrawGeometry.Get();
                params.cullingEnabled = cullingEnabled;
                params.numCascades = 0;

                GeometryPass(params);

                // Reset counts
                {
                    const Renderer::GPUVector<Renderer::IndirectDraw>& draws = _cullingResources.GetDrawCalls();
                    for (u32 i = 0; i < draws.Count(); i++)
                    {
                        Renderer::IndirectDraw& draw = draws[i];
                        draw.instanceCount = 0;
                    }
                }

                for (DrawManifest& manifest : _drawManifests)
                {
                    manifest.instanceIDs.clear();
                }
            });
    }

    _instances.Clear();
    _cullingDatas.Clear();
}

#ifdef JPH_DEBUG_RENDERER
void JoltDebugRenderer::DrawLine(JPH::RVec3Arg inFrom, JPH::RVec3Arg inTo, JPH::ColorArg inColor)
{
    vec3 from = vec3(inFrom.GetX(), inFrom.GetY(), inFrom.GetZ());
    vec3 to = vec3(inTo.GetX(), inTo.GetY(), inTo.GetZ());
    Color color = Color(inColor.r, inColor.g, inColor.b, inColor.a);

    //_debugRenderer->DrawLine3D(from, to, color);
}

void JoltDebugRenderer::DrawTriangle(JPH::RVec3Arg inV1, JPH::RVec3Arg inV2, JPH::RVec3Arg inV3, JPH::ColorArg inColor, ECastShadow inCastShadow)
{
    vec3 v1 = vec3(inV1.GetX(), inV1.GetY(), inV1.GetZ());
    vec3 v2 = vec3(inV2.GetX(), inV2.GetY(), inV2.GetZ());
    vec3 v3 = vec3(inV3.GetX(), inV3.GetY(), inV3.GetZ());
    Color color = Color(inColor.r, inColor.g, inColor.b, inColor.a);

    //_debugRenderer->DrawTriangleSolid3D(v1, v2, v3, color, true);
}

JPH::DebugRenderer::Batch JoltDebugRenderer::CreateTriangleBatch(const Vertex* inVertices, i32 inVertexCount, const u32* inIndices, i32 inIndexCount)
{
    // Creates an instanced indexed drawcall
    JoltBatch* batch = new JoltBatch();
    batch->isIndexed = true;

    // Add vertices
    u32 vertexOffset = _vertices.AddCount(inVertexCount);

    vec3 min = vec3(FLT_MAX);
    vec3 max = vec3(-FLT_MAX);

    for (i32 i = 0; i < inVertexCount; ++i)
    {
        const Vertex& vertex = inVertices[i];

        PackedVertex packedVertex;
        packedVertex.posAndUVx = vec4(vertex.mPosition.x, vertex.mPosition.y, vertex.mPosition.z, vertex.mUV.x);
        packedVertex.normalAndUVy = vec4(vertex.mNormal.x, vertex.mNormal.y, vertex.mNormal.z, vertex.mUV.y);
        packedVertex.color = vec4(vertex.mColor.r, vertex.mColor.g, vertex.mColor.b, vertex.mColor.a);

        min = glm::min(min, vec3(packedVertex.posAndUVx));
        max = glm::max(max, vec3(packedVertex.posAndUVx));

        _vertices[i] = packedVertex;
    }

    // Add indices
    u32 indexOffset = _indices.AddCount(inIndexCount);

    for (i32 i = 0; i < inIndexCount; ++i)
    {
        _indices[i] = inIndices[i];
    }

    u32 instanceIndex = _indexedCullingResources.Add();

    // Add drawcall
    const Renderer::GPUVector<Renderer::IndexedIndirectDraw>& draws = _indexedCullingResources.GetDrawCalls();
    batch->drawID = instanceIndex;

    Renderer::IndexedIndirectDraw& draw = draws[batch->drawID];
    draw.indexCount = inIndexCount;
    draw.instanceCount = 0;
    draw.firstIndex = indexOffset;
    draw.vertexOffset = vertexOffset;
    draw.firstInstance = 0; // Compact() function will set this up

    // Add drawcall data
    const Renderer::GPUVector<DrawCallData>& drawCallDatas = _indexedCullingResources.GetDrawCallDatas();

    DrawCallData& drawCallData = drawCallDatas[instanceIndex];
    drawCallData.baseInstanceLookupOffset = 0; // Compact() function will set this up

    // Add drawcall manifest
    DrawManifest& manifest = _indexedDrawManifests.emplace_back();
    manifest.center = (min + max) * 0.5f;
    manifest.extents = (max - min) * 0.5f;
    manifest.instanceIDs.clear();

    return batch;
}

JPH::DebugRenderer::Batch JoltDebugRenderer::CreateTriangleBatch(const Triangle* inTriangles, i32 inTriangleCount)
{
    // Creates an instanced non-indexed drawcall
    JoltBatch* batch = new JoltBatch();
    batch->isIndexed = false;

    // Add vertices
    u32 vertexOffset = _vertices.AddCount(inTriangleCount * 3);

    vec3 min = vec3(FLT_MAX);
    vec3 max = vec3(-FLT_MAX);

    u32 vertexIndex = 0;
    for (i32 i = 0; i < inTriangleCount; i++)
    {
        const Triangle& triangle = inTriangles[i];

        for (i32 j = 2; j >= 0; j--)
        {
            const Vertex& vertex = triangle.mV[j];

            PackedVertex packedVertex;
            packedVertex.posAndUVx = vec4(vertex.mPosition.x, vertex.mPosition.y, vertex.mPosition.z, vertex.mUV.x);
            packedVertex.normalAndUVy = vec4(vertex.mNormal.x, vertex.mNormal.y, vertex.mNormal.z, vertex.mUV.y);
            packedVertex.color = vec4(vertex.mColor.r, vertex.mColor.g, vertex.mColor.b, vertex.mColor.a);

            min = glm::min(min, vec3(packedVertex.posAndUVx));
            max = glm::max(max, vec3(packedVertex.posAndUVx));

            _vertices[vertexOffset + vertexIndex] = packedVertex;
            vertexIndex++;
        }
    }

    u32 instanceIndex = _cullingResources.Add();

    // Add drawcall
    const Renderer::GPUVector<Renderer::IndirectDraw>& draws = _cullingResources.GetDrawCalls();
    batch->drawID = instanceIndex;

    Renderer::IndirectDraw& draw = draws[batch->drawID];
    draw.vertexCount = inTriangleCount * 3;
    draw.instanceCount = 0;
    draw.firstVertex = vertexOffset;
    draw.firstInstance = 0; // Compact() function will set this up

    // Add drawcall data
    const Renderer::GPUVector<DrawCallData>& drawCallDatas = _cullingResources.GetDrawCallDatas();

    DrawCallData& drawCallData = drawCallDatas[instanceIndex];
    drawCallData.baseInstanceLookupOffset = 0; // Compact() function will set this up

    // Add drawcall manifest
    DrawManifest& manifest = _drawManifests.emplace_back();
    manifest.center = (min + max) * 0.5f;
    manifest.extents = (max - min) * 0.5f;
    manifest.instanceIDs.clear();

    return batch;
}

void JoltDebugRenderer::DrawGeometry(JPH::RMat44Arg inModelMatrix, const JPH::AABox& inWorldSpaceBounds, f32 inLODScaleSq, JPH::ColorArg inModelColor, const GeometryRef& inGeometry, ECullMode inCullMode, ECastShadow inCastShadow, EDrawMode inDrawMode)
{
    JPH::DebugRenderer::Batch batch = inGeometry->mLODs[0].mTriangleBatch;

    JoltBatch* joltBatch = static_cast<JoltBatch*>(batch.GetPtr());
    u32 drawID = joltBatch->drawID;
    bool isIndexed = joltBatch->isIndexed;

    // Add instance
    u32 instanceID = _instances.Add();
    mat4x4& instance = _instances[instanceID];

    for (u32 i = 0; i < 4; i++)
    {
        JPH::Vec4 column = inModelMatrix.GetColumn4(i);
        instance[i] = vec4(column.GetX(), column.GetY(), column.GetZ(), column.GetW());
    }

    // Add culling data
    u32 cullingDataID = _cullingDatas.Add();
    Model::ComplexModel::CullingData& cullingData = _cullingDatas[cullingDataID];

    // Increment instanceCount and add instanceID to manifest
    if (isIndexed)
    {
        const Renderer::GPUVector<Renderer::IndexedIndirectDraw>& draws = _indexedCullingResources.GetDrawCalls();
        Renderer::IndexedIndirectDraw& draw = draws[drawID];

        draw.instanceCount++;

        DrawManifest& manifest = _indexedDrawManifests[drawID];
        manifest.instanceIDs.push_back(instanceID);

        cullingData.center = hvec3(manifest.center);
        cullingData.extents = hvec3(manifest.extents);
    }
    else
    {
        const Renderer::GPUVector<Renderer::IndirectDraw>& draws = _cullingResources.GetDrawCalls();
        Renderer::IndirectDraw& draw = draws[drawID];

        draw.instanceCount++;

        DrawManifest& manifest = _drawManifests[drawID];
        manifest.instanceIDs.push_back(instanceID);

        cullingData.center = hvec3(manifest.center);
        cullingData.extents = hvec3(manifest.extents);
    }
}

void JoltDebugRenderer::DrawText3D(JPH::RVec3Arg inPosition, const std::string_view& inString, JPH::ColorArg inColor, f32 inHeight)
{
    // TODO
}
#endif

void JoltDebugRenderer::Draw(const RenderResources& resources, u8 frameIndex, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList, const DrawParams& params)
{
    Renderer::RenderPassDesc renderPassDesc;
    graphResources.InitializeRenderPassDesc(renderPassDesc);

    // Render targets
    renderPassDesc.renderTargets[0] = params.rt0;
    renderPassDesc.depthStencil = params.depth;
    commandList.BeginRenderPass(renderPassDesc);

    Renderer::GraphicsPipelineID pipeline = _drawPipeline;

    commandList.BeginPipeline(pipeline);

    for (auto& descriptorSet : params.descriptorSets)
    {
        commandList.BindDescriptorSet(*descriptorSet, frameIndex);
    }

    commandList.SetIndexBuffer(_indices.GetBuffer(), Renderer::IndexFormat::UInt16);

    if (params.cullingEnabled)
    {
        u32 drawCountBufferOffset = params.drawCountIndex * sizeof(u32);

        if (params.cullingResources->IsIndexed())
        {
            commandList.DrawIndexedIndirectCount(params.argumentBuffer, 0, params.drawCountBuffer, drawCountBufferOffset, params.numMaxDrawCalls);
        }
        else
        {
            commandList.DrawIndirectCount(params.argumentBuffer, 0, params.drawCountBuffer, drawCountBufferOffset, params.numMaxDrawCalls);
        }
    }
    else
    {
        if (params.cullingResources->IsIndexed())
        {
            commandList.DrawIndexedIndirect(params.argumentBuffer, 0, params.numMaxDrawCalls);
        }
        else
        {
            commandList.DrawIndirect(params.argumentBuffer, 0, params.numMaxDrawCalls);
        }
    }

    commandList.EndPipeline(pipeline);
    commandList.EndRenderPass(renderPassDesc);
}

void JoltDebugRenderer::CreatePermanentResources()
{
    CreatePipelines();

    _instances.SetDebugName("JoltInstances");
    _instances.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);

    _vertices.SetDebugName("JoltVertices");
    _vertices.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);

    _indices.SetDebugName("JoltIndices");
    _indices.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::INDEX_BUFFER);

    CullingResourcesIndexed<DrawCallData>::InitParams indexedInitParams;
    indexedInitParams.renderer = _renderer;
    indexedInitParams.culledRenderer = this;
    indexedInitParams.bufferNamePrefix = "IndexedJolt";
    indexedInitParams.enableTwoStepCulling = true;
    indexedInitParams.isInstanced = true;
    _indexedCullingResources.Init(indexedInitParams);

    CullingResourcesNonIndexed<DrawCallData>::InitParams initParams;
    initParams.renderer = _renderer;
    initParams.culledRenderer = this;
    initParams.bufferNamePrefix = "Jolt";
    initParams.enableTwoStepCulling = true;
    indexedInitParams.isInstanced = true;
    _cullingResources.Init(initParams);

    InitDescriptorSets();
    SyncToGPU();
}

void JoltDebugRenderer::CreatePipelines()
{
    Renderer::GraphicsPipelineDesc pipelineDesc;

    // Shader
    Renderer::VertexShaderDesc vertexShaderDesc;
    vertexShaderDesc.shaderEntry = _gameRenderer->GetShaderEntry("Jolt/Draw.vs"_h, "Jolt/Draw.vs");
    pipelineDesc.states.vertexShader = _renderer->LoadShader(vertexShaderDesc);

    Renderer::PixelShaderDesc pixelShaderDesc;
    pixelShaderDesc.shaderEntry = _gameRenderer->GetShaderEntry("Jolt/Draw.ps"_h, "Jolt/Draw.ps");
    pipelineDesc.states.pixelShader = _renderer->LoadShader(pixelShaderDesc);

    // Depth state
    pipelineDesc.states.depthStencilState.depthEnable = true;
    pipelineDesc.states.depthStencilState.depthWriteEnable = true;
    pipelineDesc.states.depthStencilState.depthFunc = Renderer::ComparisonFunc::GREATER;

    // Rasterizer state
    pipelineDesc.states.rasterizerState.cullMode = Renderer::CullMode::BACK;
    pipelineDesc.states.rasterizerState.frontFaceMode = Renderer::Settings::FRONT_FACE_STATE;

    pipelineDesc.states.renderTargetFormats[0] = Renderer::ImageFormat::R32G32_UINT; // Visibility buffer

    pipelineDesc.states.depthStencilFormat = Renderer::DepthImageFormat::D32_FLOAT;

    _drawPipeline = _renderer->CreatePipeline(pipelineDesc);
}

void JoltDebugRenderer::InitDescriptorSets()
{
    CullingResourcesBase* cullingResources[] = {
        &_indexedCullingResources,
        & _cullingResources
    };

    for (CullingResourcesBase* cullingResource : cullingResources)
    {
        Renderer::DescriptorSet& geometryPassDescriptorSet = cullingResource->GetGeometryPassDescriptorSet();
        geometryPassDescriptorSet.RegisterPipeline(_renderer, _drawPipeline);
        geometryPassDescriptorSet.Init(_renderer);
    }
}

void JoltDebugRenderer::SyncToGPU()
{
    ZoneScoped;

    CulledRenderer::SyncToGPU();

    Renderer::DescriptorSet& indexedDrawDescriptorSet = _indexedCullingResources.GetGeometryPassDescriptorSet();
    Renderer::DescriptorSet& indexedCullingDescriptorSet = _indexedCullingResources.GetCullingDescriptorSet();

    Renderer::DescriptorSet& drawDescriptorSet = _cullingResources.GetGeometryPassDescriptorSet();
    Renderer::DescriptorSet& cullingDescriptorSet = _cullingResources.GetCullingDescriptorSet();

    _instances.SetDirty();
    if (_instances.SyncToGPU(_renderer))
    {
        indexedDrawDescriptorSet.Bind("_instances", _instances.GetBuffer());
        indexedCullingDescriptorSet.Bind("_instanceMatrices", _instances.GetBuffer());

        drawDescriptorSet.Bind("_instances", _instances.GetBuffer());
        cullingDescriptorSet.Bind("_instanceMatrices", _instances.GetBuffer());
    }

    if (_vertices.SyncToGPU(_renderer))
    {
        indexedDrawDescriptorSet.Bind("_vertices", _vertices.GetBuffer());
        drawDescriptorSet.Bind("_vertices", _vertices.GetBuffer());
    }
    _indices.SyncToGPU(_renderer);

    _indexedCullingResources.SyncToGPU(false);
    _cullingResources.SyncToGPU(false);

    BindCullingResource(_indexedCullingResources);
    BindCullingResource(_cullingResources);
}

void JoltDebugRenderer::Compact()
{
    ZoneScoped;

    // Indexed
    {
        Renderer::GPUVector<InstanceRef>& instanceRefs = _indexedCullingResources.GetInstanceRefs();
        instanceRefs.Clear();

        const Renderer::GPUVector<Renderer::IndexedIndirectDraw>& draws = _indexedCullingResources.GetDrawCalls();
        const Renderer::GPUVector<DrawCallData>& drawCallDatas = _indexedCullingResources.GetDrawCallDatas();

        u32 numDraws = static_cast<u32>(draws.Count());
        u32 numInstances = 0;
        for (u32 drawID = 0; drawID < numDraws; drawID++)
        {
            Renderer::IndexedIndirectDraw& draw = draws[drawID];
            DrawCallData& drawCallData = drawCallDatas[drawID];
            DrawManifest& manifest = _indexedDrawManifests[drawID];

            for (u32 i = 0; i < manifest.instanceIDs.size(); i++)
            {
                u32 instanceID = manifest.instanceIDs[i];
                u32 instanceRefID = instanceRefs.Add();

                InstanceRef& instanceRef = instanceRefs[instanceRefID];
                instanceRef.drawID = drawID;
                instanceRef.instanceID = instanceID;
            }

            draw.firstInstance = numInstances;
            drawCallData.baseInstanceLookupOffset = numInstances;
            drawCallData.padding = draw.firstInstance; // Debug data in padding

            numInstances += draw.instanceCount;
        }
        _indexedCullingResources.SetDirty();
    }

    // Non-indexed
    {
        Renderer::GPUVector<InstanceRef>& instanceRefs = _cullingResources.GetInstanceRefs();
        instanceRefs.Clear();

        const Renderer::GPUVector<Renderer::IndirectDraw>& draws = _cullingResources.GetDrawCalls();
        const Renderer::GPUVector<DrawCallData>& drawCallDatas = _cullingResources.GetDrawCallDatas();

        u32 numDraws = static_cast<u32>(draws.Count());
        u32 numInstances = 0;
        for (u32 drawID = 0; drawID < numDraws; drawID++)
        {
            Renderer::IndirectDraw& draw = draws[drawID];
            DrawCallData& drawCallData = drawCallDatas[drawID];
            DrawManifest& manifest = _drawManifests[drawID];

            for (u32 i = 0; i < manifest.instanceIDs.size(); i++)
            {
                u32 instanceID = manifest.instanceIDs[i];
                u32 instanceRefID = instanceRefs.Add();

                InstanceRef& instanceRef = instanceRefs[instanceRefID];
                instanceRef.drawID = drawID;
                instanceRef.instanceID = instanceID;
            }

            draw.firstInstance = numInstances;
            drawCallData.baseInstanceLookupOffset = numInstances;
            drawCallData.padding = draw.firstInstance; // Debug data in padding

            numInstances += draw.instanceCount;
        }
        _cullingResources.SetDirty();
    }
}

JoltDebugRenderer::JoltBatch::JoltBatch()
{
}
