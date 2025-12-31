#include "TerrainRenderer.h"
#include "Game-Lib/Application/EnttRegistries.h"
#include "Game-Lib/ECS/Singletons/Database/TextureSingleton.h"
#include "Game-Lib/Rendering/RenderUtils.h"
#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Rendering/RenderResources.h"
#include "Game-Lib/Rendering/Debug/DebugRenderer.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Base/Util/Timer.h>
#include <Base/CVarSystem/CVarSystem.h>

#include <FileFormat/Novus/Map/MapChunk.h>

#include <Input/InputManager.h>

#include <Renderer/Renderer.h>
#include <Renderer/RenderGraph.h>
#include <Renderer/Descriptors/ImageDesc.h>

#include <imgui/imgui.h>
#include <entt/entt.hpp>

AutoCVar_Int CVAR_TerrainRendererEnabled(CVarCategory::Client | CVarCategory::Rendering, "terrainEnabled", "enable terrainrendering", 1, CVarFlags::EditCheckbox);
AutoCVar_Int CVAR_TerrainCullingEnabled(CVarCategory::Client | CVarCategory::Rendering, "terrainCulling", "enable terrain culling", 1, CVarFlags::EditCheckbox);
AutoCVar_Int CVAR_OcclusionCullingEnabled(CVarCategory::Client | CVarCategory::Rendering, "terrainOcclusionCulling", "enable terrain occlusion culling", 1, CVarFlags::EditCheckbox);

AutoCVar_Int CVAR_TerrainDisableTwoStepCulling(CVarCategory::Client | CVarCategory::Rendering, "terrainDisableTwoStepCulling", "disable two step culling and force all drawcalls into the geometry pass", 0, CVarFlags::EditCheckbox);

AutoCVar_Int CVAR_TerrainOccludersEnabled(CVarCategory::Client | CVarCategory::Rendering, "terrainDrawOccluders", "should draw occluders", 1, CVarFlags::EditCheckbox);
AutoCVar_Int CVAR_TerrainGeometryEnabled(CVarCategory::Client | CVarCategory::Rendering, "terrainDrawGeometry", "should draw geometry", 1, CVarFlags::EditCheckbox);

AutoCVar_Int CVAR_TerrainValidateTransfers(CVarCategory::Client | CVarCategory::Rendering, "terrainValidateGPUVectors", "if enabled ON START we will validate GPUVector uploads", 0, CVarFlags::EditCheckbox);

AutoCVar_Int CVAR_TerrainCastShadow(CVarCategory::Client | CVarCategory::Rendering, "shadowTerrainCastShadow", "should Terrain cast shadows", 1, CVarFlags::EditCheckbox);

TerrainRenderer::TerrainRenderer(Renderer::Renderer* renderer, GameRenderer* gameRenderer, DebugRenderer* debugRenderer)
    : _renderer(renderer)
    , _gameRenderer(gameRenderer)
    , _debugRenderer(debugRenderer)
    , _occluderFillPassDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS)
    , _resetIndirectDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS)
    , _cullingPassDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS)
    , _geometryFillPassDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS)
{
    if (CVAR_TerrainValidateTransfers.Get())
    {
        _cellIndices.SetValidation(true);
        _vertices.SetValidation(true);
        
        _instanceDatas.SetValidation(true);
        _cellDatas.SetValidation(true);
        _chunkDatas.SetValidation(true);
        _cellHeightRanges.SetValidation(true);
    }

    CreatePermanentResources();

    // Gotta keep these here to make sure they're not unused...
    _renderer->GetGPUName();
    _debugRenderer->UnProject(vec3(0, 0, 0), mat4x4(1.0f));
}

TerrainRenderer::~TerrainRenderer()
{

}

void TerrainRenderer::Update(f32 deltaTime)
{
    ZoneScoped;
    const bool cullingEnabled = true;//CVAR_TerrainCullingEnabled.Get();

    // Read back from culling counters
    u32 numDrawCalls = _instanceDatas.Count();

    for (u32 i = 0; i < Renderer::Settings::MAX_VIEWS; i++)
    {
        _numSurvivingDrawCalls[i] = numDrawCalls;
        _numOccluderDrawCalls[i] = numDrawCalls;
    }

    if (numDrawCalls > 0)
    {
        if (cullingEnabled)
        {
            u32* count = static_cast<u32*>(_renderer->MapBuffer(_occluderDrawCountReadBackBuffer));
            if (count != nullptr)
            {
                for (u32 i = 0; i < Renderer::Settings::MAX_VIEWS; i++)
                {
                    _numOccluderDrawCalls[i] = count[i];
                }
            }
            _renderer->UnmapBuffer(_occluderDrawCountReadBackBuffer);
        }

        {
            u32* count = static_cast<u32*>(_renderer->MapBuffer(_drawCountReadBackBuffer));
            if (count != nullptr)
            {
                for (u32 i = 0; i < Renderer::Settings::MAX_VIEWS; i++)
                {
                    _numSurvivingDrawCalls[i] = count[i];
                }
            }
            _renderer->UnmapBuffer(_drawCountReadBackBuffer);
        }
    }

    SyncToGPU();
}

void TerrainRenderer::AddOccluderPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex)
{
    if (!CVAR_TerrainRendererEnabled.Get())
        return;

    if (_instanceDatas.Count() == 0)
        return;

    const bool cullingEnabled = true;//CVAR_TerrainCullingEnabled.Get();
    if (!cullingEnabled)
        return;

    const bool disableTwoStepCulling = CVAR_TerrainDisableTwoStepCulling.Get();

    CVarSystem* cvarSystem = CVarSystem::Get();

    u32 numCascades = 0;
    if (CVAR_TerrainCastShadow.Get() == 1)
    {
        numCascades = *cvarSystem->GetIntCVar(CVarCategory::Client | CVarCategory::Rendering, "shadowCascadeNum");
    }

    struct Data
    {
        Renderer::ImageMutableResource visibilityBuffer;
        Renderer::DepthImageMutableResource depth[Renderer::Settings::MAX_VIEWS];

        Renderer::BufferMutableResource culledInstanceBuffer;
        Renderer::BufferMutableResource culledInstanceBitMaskBuffer;
        Renderer::BufferMutableResource prevCulledInstanceBitMaskBuffer;
        Renderer::BufferMutableResource argumentBuffer;
        Renderer::BufferMutableResource occluderDrawCountReadBackBuffer;

        Renderer::DescriptorSetResource globalSet;
        Renderer::DescriptorSetResource fillSet;
        Renderer::DescriptorSetResource drawSet;
    };

    renderGraph->AddPass<Data>("Terrain Occluders",
        [this, &resources, frameIndex, numCascades](Data& data, Renderer::RenderGraphBuilder& builder) // Setup
        {
            using BufferUsage = Renderer::BufferPassUsage;

            data.visibilityBuffer = builder.Write(resources.visibilityBuffer, Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::LOAD);
            data.depth[0] = builder.Write(resources.depth, Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::LOAD);
            for (u32 i = 1; i < numCascades + 1; i++)
            {
                data.depth[i] = builder.Write(resources.shadowDepthCascades[i-1], Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::LOAD);
            }

            builder.Read(_instanceDatas.GetBuffer(), BufferUsage::COMPUTE | BufferUsage::GRAPHICS);
            builder.Read(_vertices.GetBuffer(), BufferUsage::GRAPHICS);
            builder.Read(_cellDatas.GetBuffer(), BufferUsage::GRAPHICS);
            builder.Read(resources.cameras.GetBuffer(), BufferUsage::GRAPHICS);

            data.culledInstanceBuffer = builder.Write(_culledInstanceBuffer, BufferUsage::TRANSFER | BufferUsage::COMPUTE | BufferUsage::GRAPHICS);
            data.culledInstanceBitMaskBuffer = builder.Write(_culledInstanceBitMaskBuffer.Get(!frameIndex), BufferUsage::COMPUTE | BufferUsage::TRANSFER);
            data.prevCulledInstanceBitMaskBuffer = builder.Write(_culledInstanceBitMaskBuffer.Get(frameIndex), BufferUsage::COMPUTE | BufferUsage::TRANSFER);
            data.argumentBuffer = builder.Write(_argumentBuffer, BufferUsage::TRANSFER | BufferUsage::GRAPHICS | BufferUsage::COMPUTE);
            data.occluderDrawCountReadBackBuffer = builder.Write(_occluderDrawCountReadBackBuffer, BufferUsage::TRANSFER);

            data.globalSet = builder.Use(resources.globalDescriptorSet);
            data.fillSet = builder.Use(_occluderFillPassDescriptorSet);
            data.drawSet = builder.Use(resources.terrainDescriptorSet);

            return true; // Return true from setup to enable this pass, return false to disable it
        },
        [this, &resources, frameIndex, numCascades, disableTwoStepCulling, cullingEnabled, cvarSystem](Data& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList) // Execute
        {
            GPU_SCOPED_PROFILER_ZONE(commandList, TerrainOccluders);

            // Handle disabled occluders
            u32 cellCount = static_cast<u32>(_instanceDatas.Count());
            if (disableTwoStepCulling)
            {
                u32 bitmaskSizePerView = RenderUtils::CalcCullingBitmaskSize(cellCount);
                commandList.FillBuffer(data.culledInstanceBitMaskBuffer, 0, bitmaskSizePerView * Renderer::Settings::MAX_VIEWS, 0);
                commandList.BufferBarrier(data.culledInstanceBitMaskBuffer, Renderer::BufferPassUsage::TRANSFER);
            }

            for (u32 i = 0; i < numCascades + 1; i++)
            {
                std::string markerName = (i == 0) ? "Main" : "Cascade " + std::to_string(i - 1);
                commandList.PushMarker(markerName, Color::White);

                // Reset the counters
                {
                    commandList.BufferBarrier(data.argumentBuffer, Renderer::BufferPassUsage::TRANSFER);
                    commandList.FillBuffer(data.argumentBuffer, 4, 16, 0); // Reset everything but indexCount to 0
                    commandList.BufferBarrier(data.argumentBuffer, Renderer::BufferPassUsage::TRANSFER);
                }

                // Fill the occluders to draw
                FillDrawCallsParams fillParams;
                fillParams.passName = "Occluders";
                fillParams.cellCount = cellCount;
                fillParams.viewIndex = i;
                fillParams.diffAgainstPrev = false;
                fillParams.culledInstanceBitMaskBuffer = data.culledInstanceBitMaskBuffer;
                fillParams.prevCulledInstanceBitMaskBuffer = data.prevCulledInstanceBitMaskBuffer;
                fillParams.fillSet = data.fillSet;

                FillDrawCalls(frameIndex, graphResources, commandList, fillParams);
                commandList.BufferBarrier(data.culledInstanceBuffer, Renderer::BufferPassUsage::COMPUTE);
                commandList.BufferBarrier(data.culledInstanceBuffer, Renderer::BufferPassUsage::GRAPHICS);

                // Draw the occluders
                if (CVAR_TerrainOccludersEnabled.Get())
                {
                    commandList.PushMarker("Occlusion Draw", Color::White);

                    if (i == 1)
                    {
                        uvec2 shadowDepthDimensions = _renderer->GetImageDimensions(resources.shadowDepthCascades[0]);

                        commandList.SetViewport(0, 0, static_cast<f32>(shadowDepthDimensions.x), static_cast<f32>(shadowDepthDimensions.y), 0.0f, 1.0f);
                        commandList.SetScissorRect(0, shadowDepthDimensions.x, 0, shadowDepthDimensions.y);

                        f32 biasConstantFactor = static_cast<f32>(*cvarSystem->GetFloatCVar(CVarCategory::Client | CVarCategory::Rendering, "shadowDepthBiasConstant"));
                        f32 biasClamp = static_cast<f32>(*cvarSystem->GetFloatCVar(CVarCategory::Client | CVarCategory::Rendering, "shadowDepthBiasClamp"));
                        f32 biasSlopeFactor = static_cast<f32>(*cvarSystem->GetFloatCVar(CVarCategory::Client | CVarCategory::Rendering, "shadowDepthBiasSlope"));
                        commandList.SetDepthBias(biasConstantFactor, biasClamp, biasSlopeFactor);
                    }

                    DrawParams drawParams;
                    drawParams.shadowPass = i != 0;
                    drawParams.viewIndex = i;
                    drawParams.cullingEnabled = cullingEnabled;
                    drawParams.visibilityBuffer = data.visibilityBuffer;
                    drawParams.depth = data.depth[i];
                    drawParams.instanceBuffer = ToBufferResource(data.culledInstanceBuffer);
                    drawParams.argumentBuffer = ToBufferResource(data.argumentBuffer);

                    drawParams.globalDescriptorSet = data.globalSet;
                    drawParams.drawDescriptorSet = data.drawSet;

                    Draw(resources, frameIndex, graphResources, commandList, drawParams);

                    commandList.PopMarker();
                }

                // Copy drawn count
                {
                    u32 dstOffset = i * sizeof(u32);
                    commandList.CopyBuffer(data.occluderDrawCountReadBackBuffer, dstOffset, data.argumentBuffer, 4, 4);
                }

                commandList.PopMarker();
            }

            // Finish by resetting the viewport, scissor and depth bias
            vec2 renderSize = _renderer->GetRenderSize();
            commandList.SetViewport(0, 0, renderSize.x, renderSize.y, 0.0f, 1.0f);
            commandList.SetScissorRect(0, static_cast<u32>(renderSize.x), 0, static_cast<u32>(renderSize.y));
            commandList.SetDepthBias(0, 0, 0);
        });
}

void TerrainRenderer::AddCullingPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex)
{
    ZoneScoped;

    if (!CVAR_TerrainRendererEnabled.Get())
        return;

    const bool cullingEnabled = true;//CVAR_TerrainCullingEnabled.Get();
    if (!cullingEnabled)
        return;

    if (_instanceDatas.Count() == 0)
        return;

    u32 numCascades = *CVarSystem::Get()->GetIntCVar(CVarCategory::Client | CVarCategory::Rendering, "shadowCascadeNum"_h);

    struct Data
    {
        Renderer::ImageResource depthPyramid;

        Renderer::BufferResource prevInstanceBitMaskBuffer;

        Renderer::BufferMutableResource argumentBuffer;
        Renderer::BufferMutableResource currentInstanceBitMaskBuffer;

        Renderer::DescriptorSetResource debugSet;
        Renderer::DescriptorSetResource globalSet;
        Renderer::DescriptorSetResource resetIndirectSet;
        Renderer::DescriptorSetResource cullingSet;
    };

    renderGraph->AddPass<Data>("Terrain Culling",
        [this, &resources, frameIndex](Data& data, Renderer::RenderGraphBuilder& builder) // Setup
        {
            using BufferUsage = Renderer::BufferPassUsage;

            data.depthPyramid = builder.Read(resources.depthPyramid, Renderer::PipelineType::COMPUTE);

            data.prevInstanceBitMaskBuffer = builder.Read(_culledInstanceBitMaskBuffer.Get(!frameIndex), BufferUsage::COMPUTE);
            builder.Read(resources.cameras.GetBuffer(), BufferUsage::COMPUTE);
            builder.Read(_instanceDatas.GetBuffer(), BufferUsage::COMPUTE);
            builder.Read(_cellHeightRanges.GetBuffer(), BufferUsage::COMPUTE);
            
            data.argumentBuffer = builder.Write(_argumentBuffer, BufferUsage::COMPUTE);
            data.currentInstanceBitMaskBuffer = builder.Write(_culledInstanceBitMaskBuffer.Get(frameIndex), BufferUsage::TRANSFER | BufferUsage::COMPUTE);
            builder.Write(_culledInstanceBuffer, BufferUsage::COMPUTE);

            data.debugSet = builder.Use(_debugRenderer->GetDebugDescriptorSet());
            data.globalSet = builder.Use(resources.globalDescriptorSet);
            data.resetIndirectSet = builder.Use(_resetIndirectDescriptorSet);
            data.cullingSet = builder.Use(_cullingPassDescriptorSet);

            _debugRenderer->RegisterCullingPassBufferUsage(builder);

            return true; // Return true from setup to enable this pass, return false to disable it
        },
        [this, frameIndex, numCascades](Data& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList) // Execute
        {
            GPU_SCOPED_PROFILER_ZONE(commandList, TerrainCulling);

            // Reset indirect buffer
            {
                commandList.PushMarker("Reset indirect", Color::White);

                Renderer::ComputePipelineID pipeline = _resetIndirectBufferPipeline;
                commandList.BeginPipeline(pipeline);

                struct ResetIndirectBufferConstants
                {
                    u32 moveCountToFirst;
                };

                ResetIndirectBufferConstants* resetConstants = graphResources.FrameNew<ResetIndirectBufferConstants>();
                resetConstants->moveCountToFirst = 0; // This lets us continue building the instance buffer with 
                commandList.PushConstant(resetConstants, 0, sizeof(ResetIndirectBufferConstants));

                // Bind descriptorset
                commandList.BindDescriptorSet(data.resetIndirectSet, frameIndex);

                commandList.Dispatch(1, 1, 1);

                commandList.EndPipeline(pipeline);

                commandList.PopMarker();
            }

            // Reset the bitmask
            commandList.FillBuffer(data.currentInstanceBitMaskBuffer, 0, _culledInstanceBitMaskBufferSizePerView * Renderer::Settings::MAX_VIEWS, 0);

            commandList.BufferBarrier(data.currentInstanceBitMaskBuffer, Renderer::BufferPassUsage::TRANSFER);
            commandList.BufferBarrier(data.argumentBuffer, Renderer::BufferPassUsage::COMPUTE);

            // Cull instances on GPU
            Renderer::ComputePipelineID pipeline = _cullingPipeline;
            commandList.BeginPipeline(pipeline);

            struct CullConstants
            {
                u32 viewportSizeX;
                u32 viewportSizeY;
                u32 numCascades;
                u32 occlusionEnabled;
                u32 bitMaskBufferSizePerView;
            };

            vec2 viewportSize = _renderer->GetRenderSize();

            CullConstants* cullConstants = graphResources.FrameNew<CullConstants>();
            cullConstants->viewportSizeX = u32(viewportSize.x);
            cullConstants->viewportSizeY = u32(viewportSize.y);
            cullConstants->numCascades = numCascades;
            cullConstants->occlusionEnabled = CVAR_OcclusionCullingEnabled.Get();
            const u32 cellCount = static_cast<u32>(_cellDatas.Count());
            cullConstants->bitMaskBufferSizePerView = RenderUtils::CalcCullingBitmaskUints(cellCount);

            commandList.PushConstant(cullConstants, 0, sizeof(CullConstants));

            data.cullingSet.Bind("_depthPyramid"_h, data.depthPyramid);
            data.cullingSet.Bind("_prevCulledInstancesBitMask"_h, data.prevInstanceBitMaskBuffer);
            data.cullingSet.Bind("_culledInstancesBitMask"_h, data.currentInstanceBitMaskBuffer);

            // Bind descriptorset
            commandList.BindDescriptorSet(data.debugSet, frameIndex);
            commandList.BindDescriptorSet(data.globalSet, frameIndex);
            //commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::SHADOWS, &resources.shadowDescriptorSet, frameIndex);
            commandList.BindDescriptorSet(data.cullingSet, frameIndex);

            commandList.Dispatch((cellCount + 31) / 32, 1, 1);

            commandList.EndPipeline(pipeline);
        });
}

void TerrainRenderer::AddGeometryPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex)
{
    ZoneScoped;
    
    if (!CVAR_TerrainRendererEnabled.Get())
        return;

    if (_instanceDatas.Count() == 0)
        return;

    const bool cullingEnabled = true;//CVAR_TerrainCullingEnabled.Get();

    CVarSystem* cvarSystem = CVarSystem::Get();
    u32 numCascades = 0;
    if (CVAR_TerrainCastShadow.Get() == 1)
    {
        numCascades = *cvarSystem->GetIntCVar(CVarCategory::Client | CVarCategory::Rendering, "shadowCascadeNum");
    }

    struct Data
    {
        Renderer::ImageMutableResource visibilityBuffer;
        Renderer::DepthImageMutableResource depth[Renderer::Settings::MAX_VIEWS];

        Renderer::BufferMutableResource culledInstanceBitMaskBuffer;
        Renderer::BufferMutableResource prevCulledInstanceBitMaskBuffer;
        Renderer::BufferMutableResource culledInstanceBuffer;

        Renderer::BufferMutableResource argumentBuffer;
        Renderer::BufferMutableResource drawCountReadBackBuffer;
        Renderer::BufferMutableResource occluderDrawCountReadBackBuffer;

        Renderer::DescriptorSetResource globalSet;
        Renderer::DescriptorSetResource fillSet;
        Renderer::DescriptorSetResource geometryPassSet;
    };

    renderGraph->AddPass<Data>("Terrain Geometry",
        [this, &resources, frameIndex, cullingEnabled, numCascades](Data& data, Renderer::RenderGraphBuilder& builder)
        {
            using BufferUsage = Renderer::BufferPassUsage;

            data.visibilityBuffer = builder.Write(resources.visibilityBuffer, Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::LOAD);
            data.depth[0] = builder.Write(resources.depth, Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::LOAD);
            for (u32 i = 1; i < numCascades + 1; i++)
            {
                data.depth[i] = builder.Write(resources.shadowDepthCascades[i - 1], Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::LOAD);
            }

            data.culledInstanceBitMaskBuffer = builder.Write(_culledInstanceBitMaskBuffer.Get(frameIndex), BufferUsage::COMPUTE | BufferUsage::TRANSFER);
            data.prevCulledInstanceBitMaskBuffer = builder.Write(_culledInstanceBitMaskBuffer.Get(!frameIndex), BufferUsage::COMPUTE | BufferUsage::TRANSFER);
            data.culledInstanceBuffer = builder.Write(cullingEnabled ? _culledInstanceBuffer : _instanceDatas.GetBuffer(), BufferUsage::GRAPHICS | BufferUsage::COMPUTE);
            builder.Read(resources.cameras.GetBuffer(), BufferUsage::GRAPHICS);
            builder.Read(_vertices.GetBuffer(), BufferUsage::GRAPHICS);
            builder.Read(_cellDatas.GetBuffer(), BufferUsage::GRAPHICS);
            if (cullingEnabled)
            {
                builder.Read(_instanceDatas.GetBuffer(), BufferUsage::COMPUTE);
            }

            data.argumentBuffer = builder.Write(_argumentBuffer, BufferUsage::TRANSFER | BufferUsage::GRAPHICS | BufferUsage::COMPUTE);
            data.drawCountReadBackBuffer = builder.Write(_drawCountReadBackBuffer, BufferUsage::TRANSFER);
            data.occluderDrawCountReadBackBuffer = builder.Write(_occluderDrawCountReadBackBuffer, BufferUsage::TRANSFER); 

            data.globalSet = builder.Use(resources.globalDescriptorSet);
            data.fillSet = builder.Use(_geometryFillPassDescriptorSet);
            data.geometryPassSet = builder.Use(resources.terrainDescriptorSet);

            return true; // Return true from setup to enable this pass, return false to disable it
        },
        [this, &resources, frameIndex, cullingEnabled, numCascades, cvarSystem](Data& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList)
        {
            GPU_SCOPED_PROFILER_ZONE(commandList, TerrainGeometryPass);
            for (u32 i = 0; i < numCascades + 1; i++)
            {
                std::string markerName = (i == 0) ? "Main" : "Cascade " + std::to_string(i - 1);
                commandList.PushMarker(markerName, Color::White);

                // Reset the counters
                {
                    commandList.BufferBarrier(data.argumentBuffer, Renderer::BufferPassUsage::TRANSFER);
                    commandList.FillBuffer(data.argumentBuffer, 4, 16, 0); // Reset everything but indexCount to 0
                    commandList.BufferBarrier(data.argumentBuffer, Renderer::BufferPassUsage::TRANSFER);
                }

                if (CVAR_TerrainGeometryEnabled.Get())
                {
                    const u32 cellCount = static_cast<u32>(_cellDatas.Count());

                    // Fill the occluders to draw
                    FillDrawCallsParams fillParams;
                    fillParams.passName = "Geometry";
                    fillParams.cellCount = cellCount;
                    fillParams.viewIndex = i;
                    fillParams.diffAgainstPrev = true;
                    fillParams.culledInstanceBitMaskBuffer = data.culledInstanceBitMaskBuffer;
                    fillParams.prevCulledInstanceBitMaskBuffer = data.prevCulledInstanceBitMaskBuffer;
                    fillParams.fillSet = data.fillSet;

                    FillDrawCalls(frameIndex, graphResources, commandList, fillParams);
                    commandList.BufferBarrier(data.culledInstanceBuffer, Renderer::BufferPassUsage::COMPUTE);
                    commandList.BufferBarrier(data.culledInstanceBuffer, Renderer::BufferPassUsage::GRAPHICS);

                    if (i == 1)
                    {
                        uvec2 shadowDepthDimensions = _renderer->GetImageDimensions(resources.shadowDepthCascades[0]);

                        commandList.SetViewport(0, 0, static_cast<f32>(shadowDepthDimensions.x), static_cast<f32>(shadowDepthDimensions.y), 0.0f, 1.0f);
                        commandList.SetScissorRect(0, shadowDepthDimensions.x, 0, shadowDepthDimensions.y);

                        f32 biasConstantFactor = static_cast<f32>(*cvarSystem->GetFloatCVar(CVarCategory::Client | CVarCategory::Rendering, "shadowDepthBiasConstant"));
                        f32 biasClamp = static_cast<f32>(*cvarSystem->GetFloatCVar(CVarCategory::Client | CVarCategory::Rendering, "shadowDepthBiasClamp"));
                        f32 biasSlopeFactor = static_cast<f32>(*cvarSystem->GetFloatCVar(CVarCategory::Client | CVarCategory::Rendering, "shadowDepthBiasSlope"));
                        commandList.SetDepthBias(biasConstantFactor, biasClamp, biasSlopeFactor);
                    }

                    commandList.PushMarker("Draw", Color::White);

                    DrawParams drawParams;
                    drawParams.shadowPass = i != 0;
                    drawParams.viewIndex = i;
                    drawParams.cullingEnabled = cullingEnabled;
                    drawParams.visibilityBuffer = data.visibilityBuffer;
                    drawParams.depth = data.depth[i];
                    drawParams.instanceBuffer = ToBufferResource(data.culledInstanceBuffer);
                    drawParams.argumentBuffer = ToBufferResource(data.argumentBuffer);

                    drawParams.globalDescriptorSet = data.globalSet;
                    drawParams.drawDescriptorSet = data.geometryPassSet;

                    Draw(resources, frameIndex, graphResources, commandList, drawParams);

                    commandList.PopMarker();
                }

                if (cullingEnabled)
                {
                    u32 dstOffset = i * sizeof(u32);
                    commandList.CopyBuffer(data.drawCountReadBackBuffer, dstOffset, data.argumentBuffer, 4, 4);
                }

                commandList.PopMarker();
            }

            // Finish by resetting the viewport, scissor and depth bias
            vec2 renderSize = _renderer->GetRenderSize();
            commandList.SetViewport(0, 0, renderSize.x, renderSize.y, 0.0f, 1.0f);
            commandList.SetScissorRect(0, static_cast<u32>(renderSize.x), 0, static_cast<u32>(renderSize.y));
            commandList.SetDepthBias(0, 0, 0);
        });
}

void TerrainRenderer::Clear()
{
    ZoneScoped;
    _chunkDatas.Clear();
    _chunkBoundingBoxes.clear();
    _instanceDatas.Clear();
    _cellDatas.Clear();
    _cellHeightRanges.Clear();
    _cellBoundingBoxes.clear();
    _vertices.Clear();

    _renderer->UnloadTexturesInArray(_textures, 1);
    _renderer->UnloadTexturesInArray(_alphaTextures, 1);
}

void TerrainRenderer::Reserve(u32 numChunks)
{
    ZoneScoped;
    u32 numCells = numChunks * Terrain::CHUNK_NUM_CELLS;

    u32 chunkDataStartIndex = 0;
    u32 chunkBoundingBoxStartIndex = 0;

    u32 instanceDataStartIndex = 0;
    u32 cellDataStartIndex = 0;
    u32 cellHeightRangeStartIndex = 0;
    u32 cellBoundingBoxStartIndex = 0;

    u32 chunkVertexStartIndex = 0;

    // First we do an exclusive lock for operations that might reallocate
    {
        std::unique_lock lock(_addChunkMutex);
        _chunkDatas.Reserve(numChunks);
        _chunkBoundingBoxes.reserve(numChunks);

        _instanceDatas.Reserve(numCells);
        _cellDatas.Reserve(numCells);
        _cellHeightRanges.Reserve(numCells);
        _cellBoundingBoxes.reserve(numCells);
        
        _vertices.Reserve(numCells * Terrain::CELL_NUM_VERTICES);
    }

#if NC_DEBUG
    if (chunkDataStartIndex != chunkBoundingBoxStartIndex)
    {
        NC_LOG_ERROR("TerrainRenderer::Reserve: Chunk data start index {0} does not match chunk bounding box start index {1}, this will probably result in weird terrain", chunkDataStartIndex, chunkBoundingBoxStartIndex);
    }
    if (instanceDataStartIndex != cellDataStartIndex || instanceDataStartIndex != cellHeightRangeStartIndex || instanceDataStartIndex != cellBoundingBoxStartIndex)
    {
        NC_LOG_ERROR("TerrainRenderer::Reserve: Instance data start index {0} does not match cell data start index {1}, cell height range start index {2} or cell bounding box start index {3}, this will probably result in weird terrain", instanceDataStartIndex, cellDataStartIndex, cellHeightRangeStartIndex, cellBoundingBoxStartIndex);
    }
    if (chunkVertexStartIndex != (chunkDataStartIndex * Terrain::CHUNK_NUM_CELLS) * Terrain::CELL_NUM_VERTICES)
    {
        NC_LOG_ERROR("TerrainRenderer::Reserve: Chunk vertex start index {0} does not match chunk data index {1} * Terrain::CELL_NUM_VERTICES {2}, this will probably result in weird terrain", chunkVertexStartIndex, chunkDataStartIndex, chunkDataStartIndex * Terrain::CELL_NUM_VERTICES);
    }
#endif
}

void TerrainRenderer::AllocateChunks(u32 numChunks, TerrainReserveOffsets& reserveOffsets)
{
    ZoneScoped;
    u32 numCells = numChunks * Terrain::CHUNK_NUM_CELLS;

    u32 chunkDataStartIndex = 0;
    u32 chunkBoundingBoxStartIndex = 0;

    u32 instanceDataStartIndex = 0;
    u32 cellDataStartIndex = 0;
    u32 cellHeightRangeStartIndex = 0;
    u32 cellBoundingBoxStartIndex = 0;

    u32 chunkVertexStartIndex = 0;

    // First we do an exclusive lock for operations that might reallocate
    {
        std::unique_lock lock(_addChunkMutex);
        chunkDataStartIndex = _chunkDatas.AddCount(numChunks);
        chunkBoundingBoxStartIndex = static_cast<u32>(_chunkBoundingBoxes.size());
        _chunkBoundingBoxes.resize(_chunkBoundingBoxes.size() + numChunks);

        instanceDataStartIndex = _instanceDatas.AddCount(numCells);
        cellDataStartIndex = _cellDatas.AddCount(numCells);
        cellHeightRangeStartIndex = _cellHeightRanges.AddCount(numCells);
        cellBoundingBoxStartIndex = static_cast<u32>(_cellBoundingBoxes.size());
        _cellBoundingBoxes.resize(_cellBoundingBoxes.size() + numCells);

        chunkVertexStartIndex = _vertices.AddCount(numCells * Terrain::CELL_NUM_VERTICES);
    }

#if NC_DEBUG
    if (chunkDataStartIndex != chunkBoundingBoxStartIndex)
    {
        NC_LOG_ERROR("TerrainRenderer::Reserve: Chunk data start index {0} does not match chunk bounding box start index {1}, this will probably result in weird terrain", chunkDataStartIndex, chunkBoundingBoxStartIndex);
    }
    if (instanceDataStartIndex != cellDataStartIndex || instanceDataStartIndex != cellHeightRangeStartIndex || instanceDataStartIndex != cellBoundingBoxStartIndex)
    {
        NC_LOG_ERROR("TerrainRenderer::Reserve: Instance data start index {0} does not match cell data start index {1}, cell height range start index {2} or cell bounding box start index {3}, this will probably result in weird terrain", instanceDataStartIndex, cellDataStartIndex, cellHeightRangeStartIndex, cellBoundingBoxStartIndex);
    }
    if (chunkVertexStartIndex != (chunkDataStartIndex * Terrain::CHUNK_NUM_CELLS) * Terrain::CELL_NUM_VERTICES)
    {
        NC_LOG_ERROR("TerrainRenderer::Reserve: Chunk vertex start index {0} does not match chunk data index {1} * Terrain::CELL_NUM_VERTICES {2}, this will probably result in weird terrain", chunkVertexStartIndex, chunkDataStartIndex, chunkDataStartIndex * Terrain::CELL_NUM_VERTICES);
    }
#endif

    reserveOffsets.chunkDataStartOffset = chunkDataStartIndex;
    reserveOffsets.cellDataStartOffset = cellDataStartIndex;
    reserveOffsets.vertexDataStartOffset = chunkVertexStartIndex;
}

u32 TerrainRenderer::AddChunk(u32 chunkHash, Map::Chunk* chunk, ivec2 chunkGridPos)
{
    ZoneScoped;

    TerrainReserveOffsets reserveOffsets;
    AllocateChunks(1, reserveOffsets);

    return AddChunk(chunkHash, chunk, chunkGridPos, reserveOffsets.chunkDataStartOffset, reserveOffsets.cellDataStartOffset, reserveOffsets.vertexDataStartOffset);
}

u32 TerrainRenderer::AddChunk(u32 chunkHash, Map::Chunk* chunk, ivec2 chunkGridPos, u32 chunkDataStartOffset, u32 cellDataStartOffset, u32 vertexDataStartOffset)
{
    EnttRegistries* registries = ServiceLocator::GetEnttRegistries();
    auto& textureSingleton = registries->dbRegistry->ctx().get<ECS::Singletons::TextureSingleton>();

    // Load the chunk alpha map texture
    u32 alphaMapTextureIndex = 0;

    u32 alphaMapStringID = static_cast<u32>(chunk->chunkAlphaMapTextureHash);
    if (alphaMapStringID != std::numeric_limits<u32>().max())
    {
        if (textureSingleton.textureHashToPath.contains(alphaMapStringID))
        {
            Renderer::TextureDesc chunkAlphaMapDesc;
            chunkAlphaMapDesc.path = textureSingleton.textureHashToPath[alphaMapStringID];

            _renderer->LoadTextureIntoArray(chunkAlphaMapDesc, _alphaTextures, alphaMapTextureIndex);
        }
    }

    // Calculate chunk origin
    vec2 chunkOrigin;
    chunkOrigin.x = (chunkGridPos.x * Terrain::CHUNK_SIZE) - Terrain::MAP_HALF_SIZE;
    chunkOrigin.y = Terrain::MAP_HALF_SIZE - (chunkGridPos.y * Terrain::CHUNK_SIZE);
    vec2 flippedChunkOrigin = chunkOrigin;
    u32 chunkGridIndex = chunkGridPos.x + (chunkGridPos.y * Terrain::CHUNK_NUM_PER_MAP_STRIDE);

    // Set up texture descriptor
    u32 maxDiffuseID = 0;
    Renderer::TextureDesc textureDesc;
    textureDesc.path.resize(512);

    // Then we do a shared lock for operations that only access existing data, letting us do this part in parallel
    {
        std::shared_lock lock(_addChunkMutex);

        ChunkData& chunkData = _chunkDatas[chunkDataStartOffset];
        chunkData.alphaMapID = alphaMapTextureIndex;

        for (u32 cellID = 0; cellID < Terrain::CHUNK_NUM_CELLS; cellID++)
        {
            u32 cellDataIndex = cellDataStartOffset + cellID;

            InstanceData& instanceData = _instanceDatas[cellDataIndex];
            instanceData.packedChunkCellID = (chunkGridIndex << 16) | (cellID & 0xffff);
            instanceData.globalCellID = cellDataIndex;

            {
                std::scoped_lock lock(_packedChunkCellIDToGlobalCellIDMutex);
                _packedChunkCellIDToGlobalCellID[instanceData.packedChunkCellID] = cellDataIndex;
            }

            CellData& cellData = _cellDatas[cellDataIndex];
            cellData.hole = chunk->cellsData.holes[cellID];

            // Handle textures
            u8 layerCount = 0;
            for (const u32 layerTextureID : chunk->cellsData.layerTextureIDs[cellID])
            {
                if (layerTextureID == 0 || layerTextureID == Terrain::TEXTURE_ID_INVALID)
                {
                    break;
                }

                const std::string& texturePath = textureSingleton.textureHashToPath[layerTextureID];
                if (texturePath.size() == 0)
                    continue;

                textureDesc.path = texturePath;

                u32 diffuseID = 0;
                {
                    ZoneScopedN("LoadTexture");
                    Renderer::TextureID textureID = _renderer->LoadTextureIntoArray(textureDesc, _textures, diffuseID);
                }

                cellData.diffuseIDs[layerCount++] = diffuseID;
                maxDiffuseID = glm::max(maxDiffuseID, diffuseID);
            }

            // Copy Vertex Data
            for (u32 vertexIndex = 0; vertexIndex < Terrain::CELL_TOTAL_GRID_SIZE; vertexIndex++)
            {
                TerrainVertex& terrainVertex = _vertices[vertexDataStartOffset + (cellID * Terrain::CELL_TOTAL_GRID_SIZE) + vertexIndex];
                terrainVertex.normal[0] = chunk->cellsData.normals[cellID][vertexIndex][0];
                terrainVertex.normal[1] = chunk->cellsData.normals[cellID][vertexIndex][1];
                terrainVertex.normal[2] = chunk->cellsData.normals[cellID][vertexIndex][2];

                terrainVertex.color[0] = chunk->cellsData.colors[cellID][vertexIndex][0];
                terrainVertex.color[1] = chunk->cellsData.colors[cellID][vertexIndex][1];
                terrainVertex.color[2] = chunk->cellsData.colors[cellID][vertexIndex][2];

                terrainVertex.height = chunk->cellsData.heightField[cellID][vertexIndex];
            }

            //memcpy(&_vertices[vertexDataStartIndex + (cellID * Terrain::CELL_TOTAL_GRID_SIZE)], &cell.vertexData[0], sizeof(Map::Cell::VertexData) * Terrain::CELL_TOTAL_GRID_SIZE);

            // Calculate bounding boxes and upload height ranges
            {
                ZoneScopedN("Calculate Bounding Boxes");

                const u16 cellX = cellID % Terrain::CHUNK_NUM_CELLS_PER_STRIDE;
                const u16 cellY = cellID / Terrain::CHUNK_NUM_CELLS_PER_STRIDE;

                vec3 min;
                vec3 max;

                vec2 cellHeightBounds = chunk->cellsData.heightBounds[cellID];

                min.x = flippedChunkOrigin.x - (cellX * Terrain::CELL_SIZE);
                min.y = cellHeightBounds.x;
                min.z = flippedChunkOrigin.y - (cellY * Terrain::CELL_SIZE);

                max.x = flippedChunkOrigin.x - ((cellX + 1) * Terrain::CELL_SIZE);
                max.y = cellHeightBounds.y;
                max.z = flippedChunkOrigin.y - ((cellY + 1) * Terrain::CELL_SIZE);

                Geometry::AABoundingBox& boundingBox = _cellBoundingBoxes[cellDataIndex];
                vec3 aabbMin = glm::min(min, max);
                vec3 aabbMax = glm::max(min, max);

                boundingBox.center = (aabbMin + aabbMax) * 0.5f;
                boundingBox.extents = aabbMax - boundingBox.center;

                CellHeightRange& heightRange = _cellHeightRanges[cellDataIndex];
                heightRange.min = cellHeightBounds.x;
                heightRange.max = cellHeightBounds.y;
            }
        }

        Geometry::AABoundingBox& chunkBoundingBox = _chunkBoundingBoxes[chunkDataStartOffset];
        {
            f32 chunkMinY = chunk->heightHeader.gridMinHeight;
            f32 chunkMaxY = chunk->heightHeader.gridMaxHeight;

            f32 chunkCenterY = (chunkMinY + chunkMaxY) * 0.5f;
            f32 chunkExtentsY = chunkMaxY - chunkCenterY;

            vec2 pos = vec2(flippedChunkOrigin.x + Terrain::CHUNK_HALF_SIZE, flippedChunkOrigin.y - Terrain::CHUNK_HALF_SIZE);
            chunkBoundingBox.center = vec3(pos.x, chunkCenterY, pos.y);
            chunkBoundingBox.extents = vec3(Terrain::CHUNK_HALF_SIZE, chunkExtentsY, -Terrain::CHUNK_HALF_SIZE);
        }

        if (maxDiffuseID > Renderer::Settings::MAX_TEXTURES)
        {
            NC_LOG_CRITICAL("This is bad!");
        }
    }

    return chunkDataStartOffset;
}

void TerrainRenderer::RegisterMaterialPassBufferUsage(Renderer::RenderGraphBuilder& builder)
{
    using BufferUsage = Renderer::BufferPassUsage;

    builder.Read(_vertices.GetBuffer(), BufferUsage::COMPUTE);
    builder.Read(_instanceDatas.GetBuffer(), BufferUsage::COMPUTE);
    builder.Read(_cellDatas.GetBuffer(), BufferUsage::COMPUTE);
    builder.Read(_chunkDatas.GetBuffer(), BufferUsage::COMPUTE);
    builder.Read(_culledInstanceBuffer, BufferUsage::COMPUTE);
}

void TerrainRenderer::CreatePermanentResources()
{
    ZoneScoped;
    CreatePipelines();
    InitDescriptorSets();

    RenderResources& resources = _gameRenderer->GetRenderResources();

    Renderer::TextureArrayDesc textureArrayDesc;
    textureArrayDesc.size = Renderer::Settings::MAX_TEXTURES;

    _textures = _renderer->CreateTextureArray(textureArrayDesc);
    resources.terrainDescriptorSet.Bind("_terrainColorTextures", _textures);

    Renderer::TextureArrayDesc textureAlphaArrayDesc;
    textureAlphaArrayDesc.size = Terrain::CHUNK_NUM_PER_MAP;

    _alphaTextures = _renderer->CreateTextureArray(textureAlphaArrayDesc);
    resources.terrainDescriptorSet.Bind("_terrainAlphaTextures", _alphaTextures);

    // Create and load a 1x1 RGBA8 unorm texture with a white color
    Renderer::DataTextureDesc defaultTextureDesc;
    defaultTextureDesc.layers = 1;
    defaultTextureDesc.width = 1;
    defaultTextureDesc.height = 1;
    defaultTextureDesc.format = Renderer::ImageFormat::R8G8B8A8_UNORM;
    defaultTextureDesc.data = Renderer::DefaultWhiteTextureRGBA8Unorm;
    defaultTextureDesc.debugName = "Terrain DebugTexture";

    u32 outArraySlot = 0;
    _renderer->CreateDataTextureIntoArray(defaultTextureDesc, _textures, outArraySlot);

    // Create and load a 1x1 RGBA8 unorm texture with a black color
    defaultTextureDesc.width = 1;
    defaultTextureDesc.height = 1;
    defaultTextureDesc.layers = 2;
    defaultTextureDesc.data = Renderer::DefaultBlackTextureRGBA8Unorm;
    _renderer->CreateDataTextureIntoArray(defaultTextureDesc, _alphaTextures, outArraySlot);

    // Samplers
    Renderer::SamplerDesc alphaSamplerDesc;
    alphaSamplerDesc.enabled = true;
    alphaSamplerDesc.filter = Renderer::SamplerFilter::MIN_MAG_MIP_LINEAR;
    alphaSamplerDesc.addressU = Renderer::TextureAddressMode::CLAMP;
    alphaSamplerDesc.addressV = Renderer::TextureAddressMode::CLAMP;
    alphaSamplerDesc.addressW = Renderer::TextureAddressMode::CLAMP;
    alphaSamplerDesc.shaderVisibility = Renderer::ShaderVisibility::PIXEL;

    _alphaSampler = _renderer->CreateSampler(alphaSamplerDesc);
    resources.terrainDescriptorSet.Bind("_alphaSampler"_h, _alphaSampler);

    Renderer::SamplerDesc occlusionSamplerDesc;
    occlusionSamplerDesc.filter = Renderer::SamplerFilter::MINIMUM_MIN_MAG_MIP_LINEAR;

    occlusionSamplerDesc.addressU = Renderer::TextureAddressMode::CLAMP;
    occlusionSamplerDesc.addressV = Renderer::TextureAddressMode::CLAMP;
    occlusionSamplerDesc.addressW = Renderer::TextureAddressMode::CLAMP;
    occlusionSamplerDesc.minLOD = 0.f;
    occlusionSamplerDesc.maxLOD = 16.f;
    occlusionSamplerDesc.mode = Renderer::SamplerReductionMode::MIN;

    _occlusionSampler = _renderer->CreateSampler(occlusionSamplerDesc);
    _cullingPassDescriptorSet.Bind("_depthSampler"_h, _occlusionSampler);

    _cellIndices.SetDebugName("TerrainIndices");
    _cellIndices.SetUsage(Renderer::BufferUsage::INDEX_BUFFER);

    // Argument buffers
    {
        Renderer::BufferDesc desc;
        desc.name = "TerrainArgumentBuffer";
        desc.size = sizeof(Renderer::IndexedIndirectDraw);
        desc.usage = Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::INDIRECT_ARGUMENT_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION | Renderer::BufferUsage::TRANSFER_SOURCE;
        _argumentBuffer = _renderer->CreateBuffer(_argumentBuffer, desc);

        auto uploadBuffer = _renderer->CreateUploadBuffer(_argumentBuffer, 0, desc.size);
        memset(uploadBuffer->mappedMemory, 0, desc.size);
        static_cast<u32*>(uploadBuffer->mappedMemory)[0] = Terrain::CELL_NUM_INDICES;

        _occluderFillPassDescriptorSet.Bind("_drawCount"_h, _argumentBuffer);
        _geometryFillPassDescriptorSet.Bind("_drawCount"_h, _argumentBuffer);
        _resetIndirectDescriptorSet.Bind("_arguments"_h, _argumentBuffer);

        desc.size = sizeof(u32) * Renderer::Settings::MAX_VIEWS;
        desc.usage = Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION;
        desc.cpuAccess = Renderer::BufferCPUAccess::ReadOnly;
        _drawCountReadBackBuffer = _renderer->CreateBuffer(_drawCountReadBackBuffer, desc);
        _occluderDrawCountReadBackBuffer = _renderer->CreateBuffer(_occluderDrawCountReadBackBuffer, desc);
    }

    // Set up cell index buffer
    {
        _cellIndices.AddCount(Terrain::CELL_NUM_INDICES);
        u32 index = 0;

        for (u32 row = 0; row < Terrain::CELL_INNER_GRID_STRIDE; row++)
        {
            for (u32 col = 0; col < Terrain::CELL_INNER_GRID_STRIDE; col++)
            {
                const u32 baseVertex = (row * Terrain::CELL_GRID_ROW_SIZE + col);

                //1     2
                //   0
                //3     4

                const u32 topLeftVertex = baseVertex;
                const u32 topRightVertex = baseVertex + 1;
                const u32 bottomLeftVertex = baseVertex + Terrain::CELL_GRID_ROW_SIZE;
                const u32 bottomRightVertex = bottomLeftVertex + 1;
                const u32 centerVertex = baseVertex + Terrain::CELL_OUTER_GRID_STRIDE;

                // Up triangle
                _cellIndices[index++] = topLeftVertex;
                _cellIndices[index++] = centerVertex;
                _cellIndices[index++] = topRightVertex;
                
                // Left triangle
                _cellIndices[index++] = bottomLeftVertex;
                _cellIndices[index++] = centerVertex;
                _cellIndices[index++] = topLeftVertex;
                
                // Down triangle
                _cellIndices[index++] = bottomRightVertex;
                _cellIndices[index++] = centerVertex;
                _cellIndices[index++] = bottomLeftVertex;
                
                // Right triangle
                _cellIndices[index++] = topRightVertex;
                _cellIndices[index++] = centerVertex;
                _cellIndices[index++] = bottomRightVertex;
            }
        }
    }

    _cellIndices.SyncToGPU(_renderer);

    _vertices.SetDebugName("TerrainVertices");
    _vertices.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);

    _instanceDatas.SetDebugName("TerrainInstanceDatas");
    _instanceDatas.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);

    _cellDatas.SetDebugName("TerrainCellDatas");
    _cellDatas.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);

    _chunkDatas.SetDebugName("TerrainChunkData");
    _chunkDatas.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);
}

void TerrainRenderer::CreatePipelines()
{
    // Reset Indirect Buffer
    {
        Renderer::ComputePipelineDesc pipelineDesc;
        pipelineDesc.debugName = "Terrain Reset indirect";

        Renderer::ComputeShaderDesc shaderDesc;
        shaderDesc.shaderEntry = _gameRenderer->GetShaderEntry("Utils/ResetIndirectBuffer.cs"_h, "Utils/ResetIndirectBuffer.cs");
        pipelineDesc.computeShader = _renderer->LoadShader(shaderDesc);

        _resetIndirectBufferPipeline = _renderer->CreatePipeline(pipelineDesc);
    }
    // Fill Draw Calls
    {
        Renderer::ComputePipelineDesc pipelineDesc;
        pipelineDesc.debugName = "Terrain Fill Draw Calls";

        Renderer::ComputeShaderDesc shaderDesc;
        shaderDesc.shaderEntry = _gameRenderer->GetShaderEntry("Terrain/FillDrawCalls.cs"_h, "Terrain/FillDrawCalls.cs");
        pipelineDesc.computeShader = _renderer->LoadShader(shaderDesc);

        _fillDrawCallsPipeline = _renderer->CreatePipeline(pipelineDesc);
    }
    // Culling
    {
        Renderer::ComputePipelineDesc pipelineDesc;
        pipelineDesc.debugName = "Terrain Culling";

        Renderer::ComputeShaderDesc shaderDesc;
        shaderDesc.shaderEntry = _gameRenderer->GetShaderEntry("Terrain/Culling.cs"_h, "Terrain/Culling.cs");
        pipelineDesc.computeShader = _renderer->LoadShader(shaderDesc);

        _cullingPipeline = _renderer->CreatePipeline(pipelineDesc);
    }
    // Draw regular
    {
        Renderer::GraphicsPipelineDesc pipelineDesc;
        pipelineDesc.debugName = "Terrain Draw";

        // Shaders
        Renderer::VertexShaderDesc vertexShaderDesc;
        {
            std::vector<Renderer::PermutationField> permutationFields =
            {
                { "EDITOR_PASS", "0" },
                { "SHADOW_PASS", "0" }
            };
            u32 shaderEntryNameHash = Renderer::GetShaderEntryNameHash("Terrain/Draw.vs", permutationFields);
            vertexShaderDesc.shaderEntry = _gameRenderer->GetShaderEntry(shaderEntryNameHash, "Terrain/Draw.vs");
            pipelineDesc.states.vertexShader = _renderer->LoadShader(vertexShaderDesc);
        }

        Renderer::PixelShaderDesc pixelShaderDesc;
        {
            pixelShaderDesc.shaderEntry = _gameRenderer->GetShaderEntry("Terrain/Draw.ps"_h, "Terrain/Draw.ps");
            pipelineDesc.states.pixelShader = _renderer->LoadShader(pixelShaderDesc);
        }

        // Depth state
        pipelineDesc.states.depthStencilState.depthEnable = true;
        pipelineDesc.states.depthStencilState.depthWriteEnable = true;
        pipelineDesc.states.depthStencilState.depthFunc = Renderer::ComparisonFunc::GREATER;

        // Rasterizer state
        pipelineDesc.states.rasterizerState.cullMode = Renderer::CullMode::BACK;
        pipelineDesc.states.rasterizerState.frontFaceMode = Renderer::Settings::FRONT_FACE_STATE;

        // Render targets
        pipelineDesc.states.renderTargetFormats[0] = Renderer::ImageFormat::R32G32_UINT; // Visibility buffer format
        pipelineDesc.states.depthStencilFormat = Renderer::DepthImageFormat::D32_FLOAT;

        _drawPipeline = _renderer->CreatePipeline(pipelineDesc);
    }
    // Draw shadow
    {
        Renderer::GraphicsPipelineDesc pipelineDesc;
        pipelineDesc.debugName = "Terrain Draw Shadow";

        // Shaders
        Renderer::VertexShaderDesc vertexShaderDesc;
        {
            std::vector<Renderer::PermutationField> permutationFields =
            {
                { "EDITOR_PASS", "0" },
                { "SHADOW_PASS", "1" }
            };
            u32 shaderEntryNameHash = Renderer::GetShaderEntryNameHash("Terrain/Draw.vs", permutationFields);
            vertexShaderDesc.shaderEntry = _gameRenderer->GetShaderEntry(shaderEntryNameHash, "Terrain/Draw.vs");
            pipelineDesc.states.vertexShader = _renderer->LoadShader(vertexShaderDesc);
        }

        // Depth state
        pipelineDesc.states.depthStencilState.depthEnable = true;
        pipelineDesc.states.depthStencilState.depthWriteEnable = true;
        pipelineDesc.states.depthStencilState.depthFunc = Renderer::ComparisonFunc::GREATER;

        // Rasterizer state
        pipelineDesc.states.rasterizerState.cullMode = Renderer::CullMode::NONE;
        pipelineDesc.states.rasterizerState.frontFaceMode = Renderer::Settings::FRONT_FACE_STATE;
        pipelineDesc.states.rasterizerState.depthBiasEnabled = true;
        pipelineDesc.states.rasterizerState.depthClampEnabled = true;

        // Render targets
        pipelineDesc.states.depthStencilFormat = Renderer::DepthImageFormat::D32_FLOAT;

        _drawShadowPipeline = _renderer->CreatePipeline(pipelineDesc);
    }
}

void TerrainRenderer::InitDescriptorSets()
{
    _occluderFillPassDescriptorSet.RegisterPipeline(_renderer, _fillDrawCallsPipeline);
    _occluderFillPassDescriptorSet.Init(_renderer);

    _resetIndirectDescriptorSet.RegisterPipeline(_renderer, _resetIndirectBufferPipeline);
    _resetIndirectDescriptorSet.Init(_renderer);

    _cullingPassDescriptorSet.RegisterPipeline(_renderer, _cullingPipeline);
    _cullingPassDescriptorSet.Init(_renderer);

    _geometryFillPassDescriptorSet.RegisterPipeline(_renderer, _fillDrawCallsPipeline);
    _geometryFillPassDescriptorSet.Init(_renderer);
}

void TerrainRenderer::SyncToGPU()
{
    ZoneScoped;
    RenderResources& resources = _gameRenderer->GetRenderResources();

    if (_vertices.SyncToGPU(_renderer))
    {
        resources.terrainDescriptorSet.Bind("_packedTerrainVertices", _vertices.GetBuffer());
    }

    if (_instanceDatas.SyncToGPU(_renderer))
    {
        resources.terrainDescriptorSet.Bind("_instanceDatas", _instanceDatas.GetBuffer());
        _occluderFillPassDescriptorSet.Bind("_instances"_h, _instanceDatas.GetBuffer());
        _geometryFillPassDescriptorSet.Bind("_instances"_h, _instanceDatas.GetBuffer());
        _cullingPassDescriptorSet.Bind("_instances"_h, _instanceDatas.GetBuffer());

        {
            Renderer::BufferDesc desc;
            desc.size = sizeof(InstanceData) * static_cast<u32>(_instanceDatas.Capacity());
            desc.usage = Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::VERTEX_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION;
            desc.name = "TerrainCulledInstanceBuffer";
            _culledInstanceBuffer = _renderer->CreateBuffer(_culledInstanceBuffer, desc);

            _cullingPassDescriptorSet.Bind("_culledInstances"_h, _culledInstanceBuffer);
            _occluderFillPassDescriptorSet.Bind("_culledInstances"_h, _culledInstanceBuffer);
            _geometryFillPassDescriptorSet.Bind("_culledInstances"_h, _culledInstanceBuffer);
        }
    }

    if (_cellDatas.SyncToGPU(_renderer))
    {
        resources.terrainDescriptorSet.Bind("_packedCellData", _cellDatas.GetBuffer());

        {
            _culledInstanceBitMaskBufferSizePerView = RenderUtils::CalcCullingBitmaskSize(_cellDatas.Capacity());

            Renderer::BufferDesc desc;
            desc.size = _culledInstanceBitMaskBufferSizePerView * Renderer::Settings::MAX_VIEWS;
            desc.usage = Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION;

            for (u32 i = 0; i < _culledInstanceBitMaskBuffer.Num; i++)
            {
                desc.name = "TerrainCulledInstanceBitMaskBuffer" + std::to_string(i);
                _culledInstanceBitMaskBuffer.Get(i) = _renderer->CreateAndFillBuffer(_culledInstanceBitMaskBuffer.Get(i), desc, [](void* mappedMemory, size_t size)
                {
                    memset(mappedMemory, 0, size);
                });
            }
        }
    }

    if (_chunkDatas.SyncToGPU(_renderer))
    {
        resources.terrainDescriptorSet.Bind("_chunkData", _chunkDatas.GetBuffer());
    }

    // Sync CellHeightRanges to GPU
    {
        _cellHeightRanges.SetDebugName("CellHeightRangeBuffer");
        _cellHeightRanges.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);
        if (_cellHeightRanges.SyncToGPU(_renderer))
        {
            _cullingPassDescriptorSet.Bind("_heightRanges"_h, _cellHeightRanges.GetBuffer());
        }
    }
}

void TerrainRenderer::Draw(const RenderResources& resources, u8 frameIndex, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList, DrawParams& params)
{
    Renderer::RenderPassDesc renderPassDesc;
    graphResources.InitializeRenderPassDesc(renderPassDesc);

    // Render targets
    if (!params.shadowPass)
    {
        renderPassDesc.renderTargets[0] = params.visibilityBuffer;
    }
    renderPassDesc.depthStencil = params.depth;
    commandList.BeginRenderPass(renderPassDesc);

    // Set pipeline
    Renderer::GraphicsPipelineID pipeline = _drawPipeline;
    commandList.BeginPipeline(pipeline);

    // Set index buffer
    commandList.SetIndexBuffer(_cellIndices.GetBuffer(), Renderer::IndexFormat::UInt16);

    struct PushConstants
    {
        u32 viewIndex;
    };

    PushConstants* constants = graphResources.FrameNew<PushConstants>();
    constants->viewIndex = params.viewIndex;
    commandList.PushConstant(constants, 0, sizeof(PushConstants));

    // Bind descriptors
    params.drawDescriptorSet.Bind("_culledInstanceDatas"_h, params.instanceBuffer);

    // Bind descriptorset
    commandList.BindDescriptorSet(params.globalDescriptorSet, frameIndex);
    commandList.BindDescriptorSet(params.drawDescriptorSet, frameIndex);

    if (params.cullingEnabled)
    {
        commandList.DrawIndexedIndirect(params.argumentBuffer, 0, 1);
    }
    else
    {
        u32 cellCount = static_cast<u32>(_cellDatas.Count());
        TracyPlot("Cell Instance Count", (i64)cellCount);
        commandList.DrawIndexed(Terrain::CELL_NUM_INDICES, cellCount, 0, 0, 0);
    }

    commandList.EndPipeline(pipeline);
    commandList.EndRenderPass(renderPassDesc);
}

void TerrainRenderer::FillDrawCalls(u8 frameIndex, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList, FillDrawCallsParams& params)
{
    commandList.PushMarker(params.passName + " Fill", Color::White);

    Renderer::ComputePipelineID pipeline = _fillDrawCallsPipeline;
    commandList.BeginPipeline(pipeline);

    struct FillDrawCallConstants
    {
        u32 numTotalInstances;
        u32 bitmaskOffset;
        u32 diffAgainstPrev;
    };

    FillDrawCallConstants* fillConstants = graphResources.FrameNew<FillDrawCallConstants>();
    fillConstants->numTotalInstances = params.cellCount;

    u32 uintsNeededPerView = (params.cellCount + 31) / 32;
    fillConstants->bitmaskOffset = params.viewIndex * uintsNeededPerView;
    fillConstants->diffAgainstPrev = params.diffAgainstPrev;
    commandList.PushConstant(fillConstants, 0, sizeof(FillDrawCallConstants));

    params.fillSet.Bind("_culledInstancesBitMask"_h, params.culledInstanceBitMaskBuffer);
    params.fillSet.Bind("_prevCulledInstancesBitMask"_h, params.prevCulledInstanceBitMaskBuffer);

    // Bind descriptorset
    commandList.BindDescriptorSet(params.fillSet, frameIndex);

    commandList.Dispatch((params.cellCount + 31) / 32, 1, 1);

    commandList.EndPipeline(pipeline);
    commandList.PopMarker();
}
