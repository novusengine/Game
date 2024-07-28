#include "JoltDebugRenderer.h"

#include "Game/ECS/Singletons/JoltState.h"
#include "Game/Rendering/CullUtils.h"
#include "Game/Rendering/Debug/DebugRenderer.h"
#include "Game/Rendering/RenderUtils.h"
#include "Game/Rendering/GameRenderer.h"
#include "Game/Rendering/RenderResources.h"
#include "Game/Util/ServiceLocator.h"

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

JoltDebugRenderer::JoltDebugRenderer(Renderer::Renderer* renderer, ::DebugRenderer* debugRenderer)
    : CulledRenderer(renderer, debugRenderer)
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
    drawSettings.mDrawBoundingBox = true;
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
                builder.Write(_indexedCullingResources.GetCulledInstanceLookupTableBuffer(), BufferUsage::COMPUTE | BufferUsage::GRAPHICS);

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
                params.depth = data.depth;

                params.culledDrawCallsBuffer = data.culledDrawCallsBuffer;
                params.culledDrawCallCountBuffer = data.culledDrawCallCountBuffer;
                params.culledDrawCallsBitMaskBuffer = data.culledDrawCallsBitMaskBuffer;
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
                    drawParams.isIndexed = true;
                    Draw(resources, frameIndex, graphResources, commandList, drawParams);
                };

                params.baseInstanceLookupOffset = offsetof(DrawCallData, DrawCallData::baseInstanceLookupOffset);
                params.drawCallDataSize = sizeof(DrawCallData);

                params.enableDrawing = CVAR_JoltDebugDrawOccluders.Get();
                params.disableTwoStepCulling = CVAR_JoltDebugDisableTwoStepCulling.Get();
                params.isIndexed = true;
                params.useInstancedCulling = true;

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
                builder.Write(_cullingResources.GetCulledInstanceLookupTableBuffer(), BufferUsage::COMPUTE | BufferUsage::GRAPHICS);

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
                params.depth = data.depth;

                params.culledDrawCallsBuffer = data.culledDrawCallsBuffer;
                params.culledDrawCallCountBuffer = data.culledDrawCallCountBuffer;
                params.culledDrawCallsBitMaskBuffer = data.culledDrawCallsBitMaskBuffer;
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
                    drawParams.isIndexed = false;
                    Draw(resources, frameIndex, graphResources, commandList, drawParams);
                };

                params.baseInstanceLookupOffset = offsetof(DrawCallData, DrawCallData::baseInstanceLookupOffset);
                params.drawCallDataSize = sizeof(DrawCallData);

                params.enableDrawing = CVAR_JoltDebugDrawOccluders.Get();
                params.disableTwoStepCulling = CVAR_JoltDebugDisableTwoStepCulling.Get();
                params.isIndexed = false;
                params.useInstancedCulling = true;

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
    };

    if (_indexedCullingResources.GetDrawCalls().Size() > 0)
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
                data.culledInstanceCountsBuffer = builder.Write(_indexedCullingResources.GetCulledInstanceCountsBuffer(), BufferUsage::TRANSFER | BufferUsage::COMPUTE);
                data.culledDrawCallCountBuffer = builder.Write(_indexedCullingResources.GetCulledDrawCallCountBuffer(), BufferUsage::TRANSFER | BufferUsage::COMPUTE);
                builder.Write(_indexedCullingResources.GetCulledInstanceLookupTableBuffer(), BufferUsage::COMPUTE);

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

                params.numCascades = 0;
                params.occlusionCull = CVAR_JoltDebugOcclusionCullingEnabled.Get();

                params.cullingDataIsWorldspace = false;
                params.debugDrawColliders = CVAR_JoltDebugDrawAABBs.Get();

                params.drawCallDataSize = sizeof(DrawCallData);

                params.useInstancedCulling = true;

                CullingPass(params);
            });
    }

    if (_cullingResources.GetDrawCalls().Size() > 0)
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
                data.culledInstanceCountsBuffer = builder.Write(_cullingResources.GetCulledInstanceCountsBuffer(), BufferUsage::TRANSFER | BufferUsage::COMPUTE);
                data.culledDrawCallCountBuffer = builder.Write(_cullingResources.GetCulledDrawCallCountBuffer(), BufferUsage::TRANSFER | BufferUsage::COMPUTE);
                builder.Write(_cullingResources.GetCulledInstanceLookupTableBuffer(), BufferUsage::COMPUTE);

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

                params.useInstancedCulling = true;

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

        Renderer::BufferMutableResource culledDrawCallsBuffer;
        Renderer::BufferMutableResource culledDrawCallCountBuffer;

        Renderer::BufferMutableResource drawCountBuffer;
        Renderer::BufferMutableResource triangleCountBuffer;
        Renderer::BufferMutableResource drawCountReadBackBuffer;
        Renderer::BufferMutableResource triangleCountReadBackBuffer;

        Renderer::DescriptorSetResource globalSet;
        Renderer::DescriptorSetResource drawSet;
    };

    if (_indexedCullingResources.GetDrawCalls().Size() > 0)
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
                builder.Read(_indexedCullingResources.GetCulledInstanceLookupTableBuffer(), BufferUsage::GRAPHICS);

                data.culledDrawCallCountBuffer = builder.Write(_indexedCullingResources.GetCulledDrawCallCountBuffer(), BufferUsage::GRAPHICS);

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
                params.depth = data.depth;

                params.culledDrawCallsBuffer = data.culledDrawCallsBuffer;
                params.culledDrawCallCountBuffer = data.culledDrawCallCountBuffer;

                params.drawCountBuffer = data.drawCountBuffer;
                params.triangleCountBuffer = data.triangleCountBuffer;
                params.drawCountReadBackBuffer = data.drawCountReadBackBuffer;
                params.triangleCountReadBackBuffer = data.triangleCountReadBackBuffer;

                params.globalDescriptorSet = data.globalSet;
                params.drawDescriptorSet = data.drawSet;

                params.drawCallback = [&](DrawParams& drawParams)
                {
                    drawParams.isIndexed = true;
                    Draw(resources, frameIndex, graphResources, commandList, drawParams);
                };

                params.enableDrawing = CVAR_JoltDebugDrawGeometry.Get();
                params.cullingEnabled = cullingEnabled;
                params.useInstancedCulling = true;
                params.numCascades = 0;

                GeometryPass(params);

                // Reset counts
                {
                    std::vector<Renderer::IndexedIndirectDraw>& draws = _indexedCullingResources.GetDrawCalls().Get();
                    for (u32 i = 0; i < draws.size(); i++)
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

    if (_cullingResources.GetDrawCalls().Size() > 0)
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
                builder.Read(_cullingResources.GetCulledInstanceLookupTableBuffer(), BufferUsage::GRAPHICS);

                data.culledDrawCallCountBuffer = builder.Write(_cullingResources.GetCulledDrawCallCountBuffer(), BufferUsage::GRAPHICS);

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
                params.depth = data.depth;

                params.culledDrawCallsBuffer = data.culledDrawCallsBuffer;
                params.culledDrawCallCountBuffer = data.culledDrawCallCountBuffer;

                params.drawCountBuffer = data.drawCountBuffer;
                params.triangleCountBuffer = data.triangleCountBuffer;
                params.drawCountReadBackBuffer = data.drawCountReadBackBuffer;
                params.triangleCountReadBackBuffer = data.triangleCountReadBackBuffer;

                params.globalDescriptorSet = data.globalSet;
                params.drawDescriptorSet = data.drawSet;

                params.drawCallback = [&](DrawParams& drawParams)
                {
                    drawParams.isIndexed = false;
                    Draw(resources, frameIndex, graphResources, commandList, drawParams);
                };

                params.enableDrawing = CVAR_JoltDebugDrawGeometry.Get();
                params.cullingEnabled = cullingEnabled;
                params.useInstancedCulling = true;
                params.numCascades = 0;

                GeometryPass(params);

                // Reset counts
                {
                    std::vector<Renderer::IndirectDraw>& draws = _cullingResources.GetDrawCalls().Get();
                    for (u32 i = 0; i < draws.size(); i++)
                    {
                        Renderer::IndirectDraw& draw = draws[i];
                        draw.instanceCount = 0;
                    }
                }

                _instances.Clear(true);
                for (DrawManifest& manifest : _drawManifests)
                {
                    manifest.instanceIDs.clear();
                }
            });
    }
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
    std::vector<PackedVertex>& vertices = _vertices.Get();
    u32 vertexOffset = static_cast<u32>(vertices.size());

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

        vertices.push_back(packedVertex);
    }

    // Add indices
    std::vector<u32>& indices = _indices.Get();
    u32 indexOffset = static_cast<u32>(indices.size());

    for (i32 i = 0; i < inIndexCount; ++i)
    {
        const u32& index = inIndices[i];
        indices.push_back(index);
    }

    // Add drawcall
    std::vector<Renderer::IndexedIndirectDraw>& draws = _indexedCullingResources.GetDrawCalls().Get();
    batch->drawID = static_cast<u32>(draws.size());

    Renderer::IndexedIndirectDraw& draw = draws.emplace_back();
    draw.indexCount = inIndexCount;
    draw.instanceCount = 0;
    draw.firstIndex = indexOffset;
    draw.vertexOffset = vertexOffset;
    draw.firstInstance = 0; // Compact() function will set this up

    // Add drawcall data
    std::vector<DrawCallData>& drawCallDatas = _indexedCullingResources.GetDrawCallDatas().Get();

    DrawCallData& drawCallData = drawCallDatas.emplace_back();
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
    std::vector<PackedVertex>& vertices = _vertices.Get();
    u32 vertexOffset = static_cast<u32>(vertices.size());

    vec3 min = vec3(FLT_MAX);
    vec3 max = vec3(-FLT_MAX);

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

            vertices.push_back(packedVertex);
        }
    }

    // Add drawcall
    std::vector<Renderer::IndirectDraw>& draws = _cullingResources.GetDrawCalls().Get();
    batch->drawID = static_cast<u32>(draws.size());

    Renderer::IndirectDraw& draw = draws.emplace_back();
    draw.vertexCount = inTriangleCount * 3;
    draw.instanceCount = 0;
    draw.firstVertex = vertexOffset;
    draw.firstInstance = 0; // Compact() function will set this up

    // Add drawcall data
    std::vector<DrawCallData>& drawCallDatas = _cullingResources.GetDrawCallDatas().Get();

    DrawCallData& drawCallData = drawCallDatas.emplace_back();
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
    std::vector<mat4x4>& instances = _instances.Get();
    
    u32 instanceID = static_cast<u32>(instances.size());
    mat4x4& instance = instances.emplace_back();

    for (u32 i = 0; i < 4; i++)
    {
        JPH::Vec4 column = inModelMatrix.GetColumn4(i);
        instance[i] = vec4(column.GetX(), column.GetY(), column.GetZ(), column.GetW());
    }

    // Add culling data
    std::vector<Model::ComplexModel::CullingData>& cullingDatas = _cullingDatas.Get();
    Model::ComplexModel::CullingData& cullingData = cullingDatas.emplace_back();

    // Increment instanceCount and add instanceID to manifest
    if (isIndexed)
    {
        std::vector<Renderer::IndexedIndirectDraw>& draws = _indexedCullingResources.GetDrawCalls().Get();
        Renderer::IndexedIndirectDraw& draw = draws[drawID];

        draw.instanceCount++;

        DrawManifest& manifest = _indexedDrawManifests[drawID];
        manifest.instanceIDs.push_back(instanceID);

        cullingData.center = hvec3(manifest.center);
        cullingData.extents = hvec3(manifest.extents);
    }
    else
    {
        std::vector<Renderer::IndirectDraw>& draws = _cullingResources.GetDrawCalls().Get();
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
    Renderer::GraphicsPipelineDesc pipelineDesc;
    graphResources.InitializePipelineDesc(pipelineDesc);

    // Shader
    Renderer::VertexShaderDesc vertexShaderDesc;
    vertexShaderDesc.path = "Jolt/Draw.vs.hlsl";

    Renderer::PixelShaderDesc pixelShaderDesc;
    pixelShaderDesc.path = "Jolt/Draw.ps.hlsl";

    pipelineDesc.states.vertexShader = _renderer->LoadShader(vertexShaderDesc);
    pipelineDesc.states.pixelShader = _renderer->LoadShader(pixelShaderDesc);

    // Depth state
    pipelineDesc.states.depthStencilState.depthEnable = true;
    pipelineDesc.states.depthStencilState.depthWriteEnable = true;
    pipelineDesc.states.depthStencilState.depthFunc = Renderer::ComparisonFunc::GREATER;

    // Rasterizer state
    pipelineDesc.states.rasterizerState.cullMode = Renderer::CullMode::BACK;
    pipelineDesc.states.rasterizerState.frontFaceMode = Renderer::Settings::FRONT_FACE_STATE;

    pipelineDesc.renderTargets[0] = params.rt0;

    pipelineDesc.depthStencil = params.depth;
    Renderer::GraphicsPipelineID pipeline = _renderer->CreatePipeline(pipelineDesc);

    commandList.BeginPipeline(pipeline);

    commandList.SetIndexBuffer(_indices.GetBuffer(), Renderer::IndexFormat::UInt32);

    commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::GLOBAL, params.globalDescriptorSet, frameIndex);
    commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS, params.drawDescriptorSet, frameIndex);

    commandList.SetIndexBuffer(_indices.GetBuffer(), Renderer::IndexFormat::UInt16);

    if (params.cullingEnabled)
    {
        u32 drawCountBufferOffset = params.drawCountIndex * sizeof(u32);

        if (params.isIndexed)
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
        if (params.isIndexed)
        {
            commandList.DrawIndexedIndirect(params.argumentBuffer, 0, params.numMaxDrawCalls);
        }
        else
        {
            commandList.DrawIndirect(params.argumentBuffer, 0, params.numMaxDrawCalls);
        }
    }

    commandList.EndPipeline(pipeline);
}

void JoltDebugRenderer::CreatePermanentResources()
{
    _instances.SetDebugName("JoltInstances");
    _instances.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);

    _vertices.SetDebugName("JoltVertices");
    _vertices.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);

    _indices.SetDebugName("JoltIndices");
    _indices.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::INDEX_BUFFER);

    CullingResourcesIndexed<DrawCallData>::InitParams indexedInitParams;
    indexedInitParams.renderer = _renderer;
    indexedInitParams.bufferNamePrefix = "IndexedJolt";
    indexedInitParams.enableTwoStepCulling = true;
    _indexedCullingResources.Init(indexedInitParams);

    CullingResourcesNonIndexed<DrawCallData>::InitParams initParams;
    initParams.renderer = _renderer;
    initParams.bufferNamePrefix = "Jolt";
    initParams.enableTwoStepCulling = true;
    _cullingResources.Init(initParams);

    SyncToGPU();
}

void JoltDebugRenderer::SyncToGPU()
{
    CulledRenderer::SyncToGPU();

    Renderer::DescriptorSet& indexedDrawDescriptorSet = _indexedCullingResources.GetGeometryPassDescriptorSet();
    Renderer::DescriptorSet& indexedCullingDescriptorSet = _indexedCullingResources.GetCullingDescriptorSet();

    Renderer::DescriptorSet& drawDescriptorSet = _cullingResources.GetGeometryPassDescriptorSet();
    Renderer::DescriptorSet& cullingDescriptorSet = _cullingResources.GetCullingDescriptorSet();

    if (_instances.ForceSyncToGPU(_renderer))
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

    _indexedCullingResources.SyncToGPU();
    _cullingResources.SyncToGPU();

    BindCullingResource(_indexedCullingResources);
    BindCullingResource(_cullingResources);
}

void JoltDebugRenderer::Compact()
{
    // Indexed
    {
        _indexedCullingResources.GetInstanceRefs().Clear(false);
        std::vector<InstanceRef>& instanceRefs = _indexedCullingResources.GetInstanceRefs().Get();

        std::vector<Renderer::IndexedIndirectDraw>& draws = _indexedCullingResources.GetDrawCalls().Get();
        std::vector<DrawCallData>& drawCallDatas = _indexedCullingResources.GetDrawCallDatas().Get();

        u32 numDraws = static_cast<u32>(draws.size());
        u32 numInstances = 0;
        for (u32 drawID = 0; drawID < numDraws; drawID++)
        {
            Renderer::IndexedIndirectDraw& draw = draws[drawID];
            DrawCallData& drawCallData = drawCallDatas[drawID];
            DrawManifest& manifest = _indexedDrawManifests[drawID];

            for (u32 i = 0; i < manifest.instanceIDs.size(); i++)
            {
                u32 instanceID = manifest.instanceIDs[i];

                InstanceRef& instanceRef = instanceRefs.emplace_back();
                instanceRef.drawID = drawID;
                instanceRef.instanceID = instanceID;
            }

            draw.firstInstance = numInstances;
            drawCallData.baseInstanceLookupOffset = numInstances;
            drawCallData.padding = draw.firstInstance; // Debug data in padding

            numInstances += draw.instanceCount;
        }
        _indexedCullingResources.GetDrawCalls().SetDirty();
        _indexedCullingResources.GetDrawCallDatas().SetDirty();
        _indexedCullingResources.GetInstanceRefs().SetDirty();
    }

    // Non-indexed
    {
        _cullingResources.GetInstanceRefs().Clear(false);
        std::vector<InstanceRef>& instanceRefs = _cullingResources.GetInstanceRefs().Get();

        std::vector<Renderer::IndirectDraw>& draws = _cullingResources.GetDrawCalls().Get();
        std::vector<DrawCallData>& drawCallDatas = _cullingResources.GetDrawCallDatas().Get();

        u32 numDraws = static_cast<u32>(draws.size());
        u32 numInstances = 0;
        for (u32 drawID = 0; drawID < numDraws; drawID++)
        {
            Renderer::IndirectDraw& draw = draws[drawID];
            DrawCallData& drawCallData = drawCallDatas[drawID];
            DrawManifest& manifest = _drawManifests[drawID];

            for (u32 i = 0; i < manifest.instanceIDs.size(); i++)
            {
                u32 instanceID = manifest.instanceIDs[i];

                InstanceRef& instanceRef = instanceRefs.emplace_back();
                instanceRef.drawID = drawID;
                instanceRef.instanceID = instanceID;
            }

            draw.firstInstance = numInstances;
            drawCallData.baseInstanceLookupOffset = numInstances;
            drawCallData.padding = draw.firstInstance; // Debug data in padding

            numInstances += draw.instanceCount;
        }
        _cullingResources.GetDrawCalls().SetDirty();
        _cullingResources.GetDrawCallDatas().SetDirty();
        _cullingResources.GetInstanceRefs().SetDirty();
    }
}

JoltDebugRenderer::JoltBatch::JoltBatch()
{
}
