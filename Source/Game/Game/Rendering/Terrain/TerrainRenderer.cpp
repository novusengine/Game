#include "TerrainRenderer.h"
#include "Game/Rendering/RenderUtils.h"
#include "Game/Rendering/GameRenderer.h"
#include "Game/Rendering/RenderResources.h"
#include "Game/Rendering/Debug/DebugRenderer.h"
#include "Game/Util/ServiceLocator.h"
#include "Game/Application/EnttRegistries.h"
#include "Game/ECS/Singletons/TextureSingleton.h"

#include <Base/Util/Timer.h>
#include <Base/CVarSystem/CVarSystem.h>

#include <FileFormat/Novus/Map/MapChunk.h>

#include <Input/InputManager.h>

#include <Renderer/Renderer.h>
#include <Renderer/RenderGraph.h>
#include <Renderer/Descriptors/ImageDesc.h>

#include <imgui/imgui.h>
#include <entt/entt.hpp>

AutoCVar_Int CVAR_TerrainRendererEnabled("terrainRenderer.enabled", "enable terrainrendering", 0, CVarFlags::EditCheckbox);
AutoCVar_Int CVAR_TerrainCullingEnabled("terrainRenderer.culling", "enable terrain culling", 1, CVarFlags::EditCheckbox);
AutoCVar_Int CVAR_OcclusionCullingEnabled("terrainRenderer.occlusionculling", "enable terrain occlusion culling", 1, CVarFlags::EditCheckbox);
AutoCVar_Int CVAR_ForceDisableOccluders("terrainRenderer.forcedisableoccluders", "force disable occluders", 0, CVarFlags::EditCheckbox);

AutoCVar_Int CVAR_TerrainOccludersEnabled("terrainRenderer.draw.occluders", "should draw occluders", 1, CVarFlags::EditCheckbox);
AutoCVar_Int CVAR_TerrainGeometryEnabled("terrainRenderer.draw.geometry", "should draw geometry", 1, CVarFlags::EditCheckbox);

TerrainRenderer::TerrainRenderer(Renderer::Renderer* renderer, DebugRenderer* debugRenderer)
    : _renderer(renderer)
    , _debugRenderer(debugRenderer)
{
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

    //if (!CVAR_TerrainRendererEnabled.Get())
    //    return;

    const bool cullingEnabled = CVAR_TerrainCullingEnabled.Get();

    // Read back from culling counters
    u32 numDrawCalls = Terrain::CHUNK_NUM_CELLS * _numChunksLoaded;

    for (u32 i = 0; i < Renderer::Settings::MAX_VIEWS; i++)
    {
        _numSurvivingDrawCalls[i] = numDrawCalls;
    }

    if (cullingEnabled)
    {
        u32* count = static_cast<u32*>(_renderer->MapBuffer(_occluderDrawCountReadBackBuffer));
        if (count != nullptr)
        {
            _numOccluderDrawCalls = *count;
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

    SyncToGPU();
}

void TerrainRenderer::Clear()
{
    ZoneScoped;

    _vertices.Clear();
    _instanceDatas.Clear();
    _cellDatas.Clear();
    _chunkDatas.Clear();
    _cellHeightRanges.Clear();
}

void TerrainRenderer::AddOccluderPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex)
{
    if (!CVAR_TerrainRendererEnabled.Get())
        return;

    if (_instanceDatas.Size() == 0)
        return;

    const bool cullingEnabled = CVAR_TerrainCullingEnabled.Get();
    if (!cullingEnabled)
        return;

    const bool forceDisableOccluders = CVAR_ForceDisableOccluders.Get();

    struct TerrainOccluderPassData
    {
        Renderer::RenderPassMutableResource visibilityBuffer;
        Renderer::RenderPassMutableResource depth;
    };

    renderGraph->AddPass<TerrainOccluderPassData>("Terrain Occluders",
        [=, &resources](TerrainOccluderPassData& data, Renderer::RenderGraphBuilder& builder) // Setup
        {
            data.visibilityBuffer = builder.Write(resources.visibilityBuffer, Renderer::RenderGraphBuilder::WriteMode::RENDERTARGET, Renderer::RenderGraphBuilder::LoadMode::LOAD);
            data.depth = builder.Write(resources.depth, Renderer::RenderGraphBuilder::WriteMode::RENDERTARGET, Renderer::RenderGraphBuilder::LoadMode::LOAD);

            return true; // Return true from setup to enable this pass, return false to disable it
        },
        [=, &resources](TerrainOccluderPassData& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList) // Execute
        {
            GPU_SCOPED_PROFILER_ZONE(commandList, TerrainOccluders);

            Renderer::BufferID culledInstanceBitMaskBuffer = _culledInstanceBitMaskBuffer.Get(!frameIndex);

            // Reset the counters
            for (u32 i = 0; i < Renderer::Settings::MAX_VIEWS; i++)
            {
                u32 argumentOffset = i * sizeof(Renderer::IndexedIndirectDraw);
                commandList.FillBuffer(_argumentBuffer, argumentOffset + 4, 16, 0); // Reset everything but indexCount to 0
            }

            commandList.PipelineBarrier(Renderer::PipelineBarrierType::TransferDestToComputeShaderRW, _argumentBuffer);

            commandList.PipelineBarrier(Renderer::PipelineBarrierType::ComputeWriteToComputeShaderRead, culledInstanceBitMaskBuffer);

            u32 cellCount = static_cast<u32>(_instanceDatas.Size());

            if (forceDisableOccluders)
            {
                commandList.FillBuffer(culledInstanceBitMaskBuffer, 0, RenderUtils::CalcCullingBitmaskSize(cellCount), 0);

                commandList.PipelineBarrier(Renderer::PipelineBarrierType::TransferDestToComputeShaderRW, culledInstanceBitMaskBuffer);
            }

            // Fill the occluders to draw
            {
                commandList.PushMarker("Occlusion Fill", Color::White);

                Renderer::ComputePipelineDesc pipelineDesc;
                graphResources.InitializePipelineDesc(pipelineDesc);

                Renderer::ComputeShaderDesc shaderDesc;
                shaderDesc.path = "Terrain/FillDrawCalls.cs.hlsl";
                pipelineDesc.computeShader = _renderer->LoadShader(shaderDesc);

                Renderer::ComputePipelineID pipeline = _renderer->CreatePipeline(pipelineDesc);
                commandList.BeginPipeline(pipeline);

                struct FillDrawCallConstants
                {
                    u32 numTotalInstances;
                };

                FillDrawCallConstants* fillConstants = graphResources.FrameNew<FillDrawCallConstants>();
                fillConstants->numTotalInstances = cellCount;
                commandList.PushConstant(fillConstants, 0, sizeof(FillDrawCallConstants));

                _occluderFillPassDescriptorSet.Bind("_culledInstancesBitMask"_h, culledInstanceBitMaskBuffer);

                // Bind descriptorset
                //commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::DEBUG, &resources.debugDescriptorSet, frameIndex);
                //commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::GLOBAL, &resources.globalDescriptorSet, frameIndex);
                //commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::SHADOWS, &resources.shadowDescriptorSet, frameIndex);
                commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS, &_occluderFillPassDescriptorSet, frameIndex);

                commandList.Dispatch((cellCount + 31) / 32, 1, 1);

                commandList.EndPipeline(pipeline);
                commandList.PopMarker();
            }

            for (u32 i = 0; i < Renderer::Settings::MAX_VIEWS; i++)
            {
                commandList.PipelineBarrier(Renderer::PipelineBarrierType::ComputeWriteToVertexShaderRead, _culledInstanceBuffer[i]);
                commandList.PipelineBarrier(Renderer::PipelineBarrierType::ComputeWriteToComputeShaderRead, _culledInstanceBuffer[i]);
            }
            commandList.PipelineBarrier(Renderer::PipelineBarrierType::ComputeWriteToIndirectArguments, _argumentBuffer);

            // Draw the occluders
            if (CVAR_TerrainOccludersEnabled.Get()) 
            {
                commandList.PushMarker("Occlusion Draw", Color::White);

                DrawParams drawParams;
                drawParams.shadowPass = false;
                drawParams.cullingEnabled = cullingEnabled;
                drawParams.visibilityBuffer = data.visibilityBuffer;
                drawParams.depth = data.depth;
                drawParams.instanceBuffer = _culledInstanceBuffer[0];
                drawParams.argumentBuffer = _argumentBuffer;

                Draw(resources, frameIndex, graphResources, commandList, drawParams);

                commandList.PopMarker();
            }

            commandList.PipelineBarrier(Renderer::PipelineBarrierType::TransferDestToTransferSrc, _argumentBuffer);
            for (u32 i = 0; i < Renderer::Settings::MAX_VIEWS; i++)
            {
                u32 dstOffset = i * sizeof(u32);
                u32 argumentOffset = i * sizeof(Renderer::IndexedIndirectDraw);
                commandList.CopyBuffer(_occluderDrawCountReadBackBuffer, dstOffset, _argumentBuffer, argumentOffset + 4, 4);
                commandList.PipelineBarrier(Renderer::PipelineBarrierType::TransferDestToTransferDest, _occluderDrawCountReadBackBuffer);
            }
            commandList.PipelineBarrier(Renderer::PipelineBarrierType::TransferDestToTransferSrc, _occluderDrawCountReadBackBuffer);
        });
}

void TerrainRenderer::AddCullingPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex)
{
    ZoneScoped;

    if (!CVAR_TerrainRendererEnabled.Get())
        return;

    if (!CVAR_TerrainCullingEnabled.Get())
        return;

    if (_instanceDatas.Size() == 0)
        return;

    u32 numCascades = 0;// *CVarSystem::Get()->GetIntCVar("shadows.cascade.num");

    struct TerrainCullingPassData
    {
    };

    renderGraph->AddPass<TerrainCullingPassData>("Terrain Culling",
        [=, &resources](TerrainCullingPassData& data, Renderer::RenderGraphBuilder& builder) // Setup
        {
            return true; // Return true from setup to enable this pass, return false to disable it
        },
        [=, &resources](TerrainCullingPassData& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList) // Execute
        {
            GPU_SCOPED_PROFILER_ZONE(commandList, TerrainCulling);

            // Reset indirect buffer
            {
                commandList.PushMarker("Reset indirect", Color::White);

                Renderer::ComputePipelineDesc pipelineDesc;
                graphResources.InitializePipelineDesc(pipelineDesc);

                Renderer::ComputeShaderDesc shaderDesc;
                shaderDesc.path = "Utils/resetIndirectBuffer.cs.hlsl";
                pipelineDesc.computeShader = _renderer->LoadShader(shaderDesc);

                Renderer::ComputePipelineID pipeline = _renderer->CreatePipeline(pipelineDesc);
                commandList.BeginPipeline(pipeline);

                struct ResetIndirectBufferConstants
                {
                    bool moveCountToFirst;
                };

                ResetIndirectBufferConstants* resetConstants = graphResources.FrameNew<ResetIndirectBufferConstants>();
                resetConstants->moveCountToFirst = true; // This lets us continue building the instance buffer with 
                commandList.PushConstant(resetConstants, 0, 4);

                // Bind descriptorset
                commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS, &_cullingPassDescriptorSet, frameIndex);

                commandList.Dispatch(1, 1, 1);

                commandList.EndPipeline(pipeline);

                commandList.PipelineBarrier(Renderer::PipelineBarrierType::ComputeWriteToComputeShaderRead, _argumentBuffer);
                commandList.PopMarker();
            }

            // Cull instances on GPU
            Renderer::ComputePipelineDesc pipelineDesc;
            graphResources.InitializePipelineDesc(pipelineDesc);

            Renderer::ComputeShaderDesc shaderDesc;
            shaderDesc.path = "Terrain/Culling.cs.hlsl";
            pipelineDesc.computeShader = _renderer->LoadShader(shaderDesc);

            Renderer::ComputePipelineID pipeline = _renderer->CreatePipeline(pipelineDesc);
            commandList.BeginPipeline(pipeline);

            Renderer::BufferID currentInstanceBitMaskBuffer = _culledInstanceBitMaskBuffer.Get(frameIndex);

            // Reset the bitmask
            const u32 cellCount = static_cast<u32>(_cellDatas.Size());
            u32 bitmaskSize = RenderUtils::CalcCullingBitmaskSize(cellCount);
            commandList.FillBuffer(currentInstanceBitMaskBuffer, 0, bitmaskSize, 0);
            commandList.PipelineBarrier(Renderer::PipelineBarrierType::TransferDestToComputeShaderRW, currentInstanceBitMaskBuffer);

            struct CullConstants
            {
                u32 numCascades;
                u32 occlusionEnabled;
            };

            CullConstants* cullConstants = graphResources.FrameNew<CullConstants>();
            cullConstants->numCascades = numCascades;
            cullConstants->occlusionEnabled = CVAR_OcclusionCullingEnabled.Get();

            commandList.PushConstant(cullConstants, 0, sizeof(CullConstants));

            commandList.ImageBarrier(resources.depthPyramid);
            _cullingPassDescriptorSet.Bind("_depthPyramid"_h, resources.depthPyramid);
            _cullingPassDescriptorSet.Bind("_prevCulledInstancesBitMask"_h, _culledInstanceBitMaskBuffer.Get(!frameIndex));
            _cullingPassDescriptorSet.Bind("_culledInstancesBitMask"_h, _culledInstanceBitMaskBuffer.Get(frameIndex));

            // Bind descriptorset
            commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::DEBUG, &_debugRenderer->GetDebugDescriptorSet(), frameIndex);
            commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::GLOBAL, &resources.globalDescriptorSet, frameIndex);
            //commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::SHADOWS, &resources.shadowDescriptorSet, frameIndex);
            commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::TERRAIN, &_cullingPassDescriptorSet, frameIndex);

            
            commandList.Dispatch((cellCount + 31) / 32, 1, 1);

            commandList.EndPipeline(pipeline);
        });
}

void TerrainRenderer::AddGeometryPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex)
{
    ZoneScoped;
    
    if (!CVAR_TerrainRendererEnabled.Get())
        return;

    if (_instanceDatas.Size() == 0)
        return;

    const bool cullingEnabled = CVAR_TerrainCullingEnabled.Get();

    struct Data
    {
        Renderer::RenderPassMutableResource visibilityBuffer;
        Renderer::RenderPassMutableResource depth;
    };

    renderGraph->AddPass<Data>("TerrainGeometry",
        [=, &resources](Data& data, Renderer::RenderGraphBuilder& builder)
        {
            data.visibilityBuffer = builder.Write(resources.visibilityBuffer, Renderer::RenderGraphBuilder::WriteMode::RENDERTARGET, Renderer::RenderGraphBuilder::LoadMode::LOAD);
            data.depth = builder.Write(resources.depth, Renderer::RenderGraphBuilder::WriteMode::RENDERTARGET, Renderer::RenderGraphBuilder::LoadMode::LOAD);

            return true; // Return true from setup to enable this pass, return false to disable it
        },
        [=, &resources](Data& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList)
        {
            GPU_SCOPED_PROFILER_ZONE(commandList, TerrainGeometryPass);

            // Add barriers if we're culling
            if (cullingEnabled)
            {
                for (u32 i = 0; i < Renderer::Settings::MAX_VIEWS; i++)
                {
                    commandList.PipelineBarrier(Renderer::PipelineBarrierType::ComputeWriteToVertexShaderRead, _culledInstanceBuffer[i]);
                    commandList.PipelineBarrier(Renderer::PipelineBarrierType::ComputeWriteToPixelShaderRead, _culledInstanceBuffer[i]);
                    commandList.PipelineBarrier(Renderer::PipelineBarrierType::ComputeWriteToComputeShaderRead, _culledInstanceBuffer[i]);
                }
                commandList.PipelineBarrier(Renderer::PipelineBarrierType::ComputeWriteToIndirectArguments, _argumentBuffer);
            }

            const Renderer::BufferID instanceBuffer = cullingEnabled ? _culledInstanceBuffer[0] : _instanceDatas.GetBuffer();
            _materialPassDescriptorSet.Bind("_instanceDatas"_h, instanceBuffer);

            if (CVAR_TerrainGeometryEnabled.Get())
            {
                DrawParams drawParams;
                drawParams.shadowPass = false;
                drawParams.cullingEnabled = cullingEnabled;
                drawParams.visibilityBuffer = data.visibilityBuffer;
                drawParams.depth = data.depth;
                drawParams.instanceBuffer = instanceBuffer;
                drawParams.argumentBuffer = _argumentBuffer;

                Draw(resources, frameIndex, graphResources, commandList, drawParams);
            }

            if (cullingEnabled)
            {
                commandList.PipelineBarrier(Renderer::PipelineBarrierType::TransferDestToTransferSrc, _argumentBuffer);
                for (u32 i = 0; i < Renderer::Settings::MAX_VIEWS; i++)
                {
                    u32 dstOffset = i * sizeof(u32);
                    u32 argumentOffset = i * sizeof(Renderer::IndexedIndirectDraw);
                    commandList.CopyBuffer(_drawCountReadBackBuffer, dstOffset, _argumentBuffer, argumentOffset + 4, 4);
                    commandList.PipelineBarrier(Renderer::PipelineBarrierType::TransferDestToTransferDest, _occluderDrawCountReadBackBuffer);
                }
                commandList.PipelineBarrier(Renderer::PipelineBarrierType::TransferDestToTransferSrc, _drawCountReadBackBuffer);
            }

            /*Renderer::GraphicsPipelineDesc pipelineDesc;
            graphResources.InitializePipelineDesc(pipelineDesc);

            // Shaders
            Renderer::VertexShaderDesc vertexShaderDesc;
            vertexShaderDesc.path = "Terrain/Draw.vs.hlsl";
            vertexShaderDesc.AddPermutationField("EDITOR_PASS", "0");
            vertexShaderDesc.AddPermutationField("SHADOW_PASS", "0");

            pipelineDesc.states.vertexShader = _renderer->LoadShader(vertexShaderDesc);

            Renderer::PixelShaderDesc pixelShaderDesc;
            pixelShaderDesc.path = "Terrain/Draw.ps.hlsl";

            pipelineDesc.states.pixelShader = _renderer->LoadShader(pixelShaderDesc);

            // Depth state
            pipelineDesc.states.depthStencilState.depthEnable = true;
            pipelineDesc.states.depthStencilState.depthWriteEnable = true;
            pipelineDesc.states.depthStencilState.depthFunc = Renderer::ComparisonFunc::GREATER;

            // Rasterizer state
            pipelineDesc.states.rasterizerState.cullMode = Renderer::CullMode::BACK;
            pipelineDesc.states.rasterizerState.frontFaceMode = Renderer::FrontFaceState::COUNTERCLOCKWISE;

            // Render targets
            pipelineDesc.renderTargets[0] = data.visibilityBuffer;
            pipelineDesc.depthStencil = data.depth;

            // Set pipeline
            Renderer::GraphicsPipelineID pipeline = _renderer->CreatePipeline(pipelineDesc); // This will compile the pipeline and return the ID, or just return ID of cached pipeline
            commandList.BeginPipeline(pipeline);

            commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::GLOBAL, &resources.globalDescriptorSet, frameIndex);
            commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::TERRAIN, &_geometryPassDescriptorSet, frameIndex);

            commandList.SetIndexBuffer(_cellIndices.GetBuffer(), Renderer::IndexFormat::UInt16);

            commandList.DrawIndexed(static_cast<u32>(_cellIndices.Size()), static_cast<u32>(_instanceDatas.Size()), 0, 0, 0);

            commandList.EndPipeline(pipeline);*/
        });
}

void TerrainRenderer::ClearChunks()
{
    _numChunksLoaded = 0;

    _chunkDatas.Clear();
    _chunkBoundingBoxes.clear();
    _instanceDatas.Clear();
    _cellDatas.Clear();
    _cellHeightRanges.Clear();
    _cellBoundingBoxes.clear();
    _vertices.Clear();

    _renderer->UnloadTexturesInArray(_textures, 0);
}

void TerrainRenderer::ReserveChunks(u32 numChunks)
{
    u32 totalNumCells = numChunks * Terrain::CHUNK_NUM_CELLS;
    u32 totalNumVertices = totalNumCells * Terrain::CELL_TOTAL_GRID_SIZE;

    _chunkDatas.Grow(numChunks);
    _chunkBoundingBoxes.resize(_chunkBoundingBoxes.size() + numChunks);
    _instanceDatas.Grow(totalNumCells);
    _cellDatas.Grow(totalNumCells);
    _cellHeightRanges.Grow(totalNumCells);
    _cellBoundingBoxes.resize(_cellBoundingBoxes.size() + totalNumCells);

    _vertices.Grow(totalNumVertices);
}

u32 TerrainRenderer::AddChunk(u32 chunkHash, Map::Chunk* chunk, ivec2 chunkGridPos)
{
    u32 currentChunkIndex = _numChunksLoaded.fetch_add(1);
    u32 currentChunkCellIndex = currentChunkIndex * Terrain::CHUNK_NUM_CELLS;
    u32 currentChunkVertexIndex = currentChunkCellIndex * Terrain::CELL_TOTAL_GRID_SIZE;

    EnttRegistries* registries = ServiceLocator::GetEnttRegistries();
    entt::registry* registry = registries->gameRegistry;

    entt::registry::context& ctx = registry->ctx();
    ECS::Singletons::TextureSingleton& textureSingleton = ctx.at<ECS::Singletons::TextureSingleton>();

    ChunkData& chunkData = _chunkDatas.Get()[currentChunkIndex];

    std::vector<InstanceData>& instanceDatas = _instanceDatas.Get();
    std::vector<TerrainVertex>& vertices = _vertices.Get();
    std::vector<CellData>& cellDatas = _cellDatas.Get();
    std::vector<CellHeightRange>& cellHeightRanges = _cellHeightRanges.Get();
    std::vector<Geometry::AABoundingBox>& cellBoundingBoxes = _cellBoundingBoxes;
    std::vector<Geometry::AABoundingBox>& chunkBoundingBoxes = _chunkBoundingBoxes;

    u32 alphaMapStringID = chunk->chunkAlphaMapTextureHash;
    if (alphaMapStringID != std::numeric_limits<u32>().max())
    {
        Renderer::TextureDesc chunkAlphaMapDesc;
        chunkAlphaMapDesc.path = textureSingleton.textureHashToPath[alphaMapStringID];

        _renderer->LoadTextureIntoArray(chunkAlphaMapDesc, _alphaTextures, chunkData.alphaMapID);
    }

    vec2 chunkOrigin;
    chunkOrigin.x = Terrain::MAP_HALF_SIZE - (chunkGridPos.x * Terrain::CHUNK_SIZE);
    chunkOrigin.y = Terrain::MAP_HALF_SIZE - (chunkGridPos.y * Terrain::CHUNK_SIZE);
    vec2 flippedChunkOrigin = chunkOrigin;
    u32 chunkGridIndex = chunkGridPos.x + (chunkGridPos.y * Terrain::CHUNK_NUM_PER_MAP_STRIDE);

    u32 maxDiffuseID = 0;

    Renderer::TextureDesc textureDesc;
    textureDesc.path.resize(512);

    for (u32 cellID = 0; cellID < Terrain::CHUNK_NUM_CELLS; cellID++)
    {
        const Map::Cell& cell = chunk->cells[cellID];

        u32 cellIndex = currentChunkCellIndex + cellID;

        InstanceData& instanceData = instanceDatas[cellIndex];
        instanceData.packedChunkCellID = (chunkGridIndex << 16) | (cellID & 0xffff);
        instanceData.globalCellID = cellIndex;

        CellData& cellData = cellDatas[cellIndex];
        cellData.hole = cell.hole;

        // Handle textures
        u8 layerCount = 0;
        for (const Map::Cell::LayerData& layer : cell.layers)
        {
            if (layer.textureID == 0 || layer.textureID == Terrain::TEXTURE_ID_INVALID)
            {
                break;
            }

            const std::string& texturePath = textureSingleton.textureHashToPath[layer.textureID];
            if (texturePath.size() == 0)
                continue;

            textureDesc.path = texturePath;

            u32 diffuseID = 0;
            {
                ZoneScopedN("LoadTexture");
                Renderer::TextureID textureID = _renderer->LoadTextureIntoArray(textureDesc, _textures, diffuseID);

                textureSingleton.textureHashToTextureID[layer.textureID] = static_cast<Renderer::TextureID::type>(textureID);
            }

            cellData.diffuseIDs[layerCount++] = diffuseID;
            maxDiffuseID = glm::max(maxDiffuseID, diffuseID);
        }

        // Copy Vertex Data
        memcpy(&vertices[currentChunkVertexIndex + (cellID * Terrain::CELL_TOTAL_GRID_SIZE)], &cell.vertexData[0], sizeof(Map::Cell::VertexData) * Terrain::CELL_TOTAL_GRID_SIZE);

        // Calculate bounding boxes and upload height ranges
        {
            ZoneScopedN("Calculate Bounding Boxes");

            const u16 cellX = cellID / Terrain::CHUNK_NUM_CELLS_PER_STRIDE;
            const u16 cellY = cellID % Terrain::CHUNK_NUM_CELLS_PER_STRIDE;

            vec3 min;
            vec3 max;

            min.x = flippedChunkOrigin.x - (cellY * Terrain::CELL_SIZE);
            min.y = cell.cellMinHeight;
            min.z = flippedChunkOrigin.y - (cellX * Terrain::CELL_SIZE);

            max.x = flippedChunkOrigin.x - ((cellY + 1) * Terrain::CELL_SIZE);
            max.y = cell.cellMaxHeight;
            max.z = flippedChunkOrigin.y - ((cellX + 1) * Terrain::CELL_SIZE);

            Geometry::AABoundingBox& boundingBox = cellBoundingBoxes[cellIndex];
            vec3 aabbMin = glm::min(min, max);
            vec3 aabbMax = glm::max(min, max);

            boundingBox.center = (aabbMin + aabbMax) * 0.5f;
            boundingBox.extents = aabbMax - boundingBox.center;

            CellHeightRange& heightRange = cellHeightRanges[cellIndex];
            heightRange.min = cell.cellMinHeight;
            heightRange.max = cell.cellMaxHeight;
        }
    }

    Geometry::AABoundingBox& chunkBoundingBox = chunkBoundingBoxes[currentChunkIndex];
    {
        f32 chunkMinY = chunk->heightHeader.gridMinHeight;
        f32 chunkMaxY = chunk->heightHeader.gridMaxHeight;

        f32 chunkCenterY = (chunkMinY + chunkMaxY) * 0.5f;
        f32 chunkExtentsY = chunkMaxY - chunkCenterY;

        vec2 pos = flippedChunkOrigin - Terrain::MAP_HALF_SIZE;
        chunkBoundingBox.center = vec3(pos.x, chunkCenterY, pos.y);
        chunkBoundingBox.extents = vec3(Terrain::CHUNK_HALF_SIZE, chunkExtentsY, Terrain::CHUNK_HALF_SIZE);
    }

    if (maxDiffuseID > 4096)
    {
        DebugHandler::PrintFatal("This is bad!");
    }

    return 0;
}

void TerrainRenderer::CreatePermanentResources()
{
    ZoneScoped;
    Renderer::TextureArrayDesc textureArrayDesc;
    textureArrayDesc.size = 4096;

    _textures = _renderer->CreateTextureArray(textureArrayDesc);
    _materialPassDescriptorSet.Bind("_terrainColorTextures", _textures);

    Renderer::TextureArrayDesc textureAlphaArrayDesc;
    textureAlphaArrayDesc.size = Terrain::CHUNK_NUM_PER_MAP;

    _alphaTextures = _renderer->CreateTextureArray(textureAlphaArrayDesc);
    _materialPassDescriptorSet.Bind("_terrainAlphaTextures", _alphaTextures);

    // Create and load a 1x1 pixel RGBA8 unorm texture with zero'ed data so we can use textureArray[0] as "invalid" textures, sampling it will return 0.0f on all channels
    Renderer::DataTextureDesc zeroColorTextureDesc;
    zeroColorTextureDesc.layers = 1;
    zeroColorTextureDesc.width = 1;
    zeroColorTextureDesc.height = 1;
    zeroColorTextureDesc.format = Renderer::ImageFormat::R8G8B8A8_UNORM;
    zeroColorTextureDesc.data = new u8[8]{ 200, 200, 200, 255, 0, 0, 0, 0 };
    zeroColorTextureDesc.debugName = "Terrain DebugTexture";

    u32 outArraySlot = 0;
    _renderer->CreateDataTextureIntoArray(zeroColorTextureDesc, _textures, outArraySlot);

    zeroColorTextureDesc.layers = 2;
    _renderer->CreateDataTextureIntoArray(zeroColorTextureDesc, _alphaTextures, outArraySlot);

    delete[] zeroColorTextureDesc.data;

    // Samplers
    Renderer::SamplerDesc alphaSamplerDesc;
    alphaSamplerDesc.enabled = true;
    alphaSamplerDesc.filter = Renderer::SamplerFilter::MIN_MAG_MIP_LINEAR;
    alphaSamplerDesc.addressU = Renderer::TextureAddressMode::CLAMP;
    alphaSamplerDesc.addressV = Renderer::TextureAddressMode::CLAMP;
    alphaSamplerDesc.addressW = Renderer::TextureAddressMode::CLAMP;
    alphaSamplerDesc.shaderVisibility = Renderer::ShaderVisibility::PIXEL;

    _alphaSampler = _renderer->CreateSampler(alphaSamplerDesc);
    _materialPassDescriptorSet.Bind("_alphaSampler"_h, _alphaSampler);

    Renderer::SamplerDesc colorSamplerDesc;
    colorSamplerDesc.enabled = true;
    colorSamplerDesc.filter = Renderer::SamplerFilter::MIN_MAG_MIP_LINEAR;
    colorSamplerDesc.addressU = Renderer::TextureAddressMode::WRAP;
    colorSamplerDesc.addressV = Renderer::TextureAddressMode::WRAP;
    colorSamplerDesc.addressW = Renderer::TextureAddressMode::CLAMP;
    colorSamplerDesc.shaderVisibility = Renderer::ShaderVisibility::PIXEL;

    _colorSampler = _renderer->CreateSampler(colorSamplerDesc);
    _materialPassDescriptorSet.Bind("_colorSampler"_h, _colorSampler);

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
        desc.size = sizeof(Renderer::IndexedIndirectDraw) * Renderer::Settings::MAX_VIEWS;
        desc.usage = Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::INDIRECT_ARGUMENT_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION | Renderer::BufferUsage::TRANSFER_SOURCE;
        _argumentBuffer = _renderer->CreateBuffer(_argumentBuffer, desc);

        auto uploadBuffer = _renderer->CreateUploadBuffer(_argumentBuffer, 0, desc.size);
        memset(uploadBuffer->mappedMemory, 0, desc.size);
        for (u32 i = 0; i < Renderer::Settings::MAX_VIEWS; i++)
        {
            u32 argumentOffset = i * (sizeof(Renderer::IndexedIndirectDraw) / sizeof(u32));
            static_cast<u32*>(uploadBuffer->mappedMemory)[argumentOffset] = Terrain::CELL_NUM_INDICES;
        }

        _occluderFillPassDescriptorSet.Bind("_drawCount"_h, _argumentBuffer);
        _cullingPassDescriptorSet.Bind("_arguments"_h, _argumentBuffer);

        desc.size = sizeof(u32) * Renderer::Settings::MAX_VIEWS;
        desc.usage = Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION;
        desc.cpuAccess = Renderer::BufferCPUAccess::ReadOnly;
        _drawCountReadBackBuffer = _renderer->CreateBuffer(_drawCountReadBackBuffer, desc);
        _occluderDrawCountReadBackBuffer = _renderer->CreateBuffer(_occluderDrawCountReadBackBuffer, desc);
    }

    // Set up cell index buffer
    {
        std::vector<u16>& indices = _cellIndices.Get();
        indices.reserve(Terrain::CELL_NUM_INDICES);

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
                indices.push_back(centerVertex);
                indices.push_back(topRightVertex);
                indices.push_back(topLeftVertex);

                // Left triangle
                indices.push_back(centerVertex);
                indices.push_back(topLeftVertex);
                indices.push_back(bottomLeftVertex);

                // Down triangle
                indices.push_back(centerVertex);
                indices.push_back(bottomLeftVertex);
                indices.push_back(bottomRightVertex);

                // Right triangle
                indices.push_back(centerVertex);
                indices.push_back(bottomRightVertex);
                indices.push_back(topRightVertex);
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

void TerrainRenderer::SyncToGPU()
{
    if (_vertices.SyncToGPU(_renderer))
    {
        _geometryPassDescriptorSet.Bind("_packedTerrainVertices", _vertices.GetBuffer());
        _materialPassDescriptorSet.Bind("_packedTerrainVertices", _vertices.GetBuffer());
    }

    if (_instanceDatas.SyncToGPU(_renderer))
    {
        _geometryPassDescriptorSet.Bind("_instanceDatas", _instanceDatas.GetBuffer());
        _materialPassDescriptorSet.Bind("_instanceDatas", _instanceDatas.GetBuffer());
        _occluderFillPassDescriptorSet.Bind("_instances"_h, _instanceDatas.GetBuffer());
        _cullingPassDescriptorSet.Bind("_instances"_h, _instanceDatas.GetBuffer());

        {
            Renderer::BufferDesc desc;
            desc.size = sizeof(InstanceData) * static_cast<u32>(_instanceDatas.Size());
            desc.usage = Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::VERTEX_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION;

            // CulledDrawCallBuffer, one for each view
            for (u32 i = 0; i < Renderer::Settings::MAX_VIEWS; i++)
            {
                desc.name = "TerrainCulledInstanceBuffer" + std::to_string(i);
                _culledInstanceBuffer[i] = _renderer->CreateBuffer(_culledInstanceBuffer[i], desc);

                _cullingPassDescriptorSet.BindArray("_culledInstances"_h, _culledInstanceBuffer[i], i);
            }

            _occluderFillPassDescriptorSet.Bind("_culledInstances"_h, _culledInstanceBuffer[0]);
        }
    }

    if (_cellDatas.SyncToGPU(_renderer))
    {
        _geometryPassDescriptorSet.Bind("_packedCellData", _cellDatas.GetBuffer());
        _materialPassDescriptorSet.Bind("_packedCellData", _cellDatas.GetBuffer());

        {
            Renderer::BufferDesc desc;
            desc.name = "TerrainCulledInstanceBitMaskBuffer";
            desc.size = RenderUtils::CalcCullingBitmaskSize(_cellDatas.Size());
            desc.usage = Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION;

            for (u32 i = 0; i < _culledInstanceBitMaskBuffer.Num; i++)
            {
                _culledInstanceBitMaskBuffer.Get(i) = _renderer->CreateAndFillBuffer(_culledInstanceBitMaskBuffer.Get(i), desc, [](void* mappedMemory, size_t size)
                {
                    memset(mappedMemory, 0, size);
                });
            }
        }
    }

    if (_chunkDatas.SyncToGPU(_renderer))
    {
        _materialPassDescriptorSet.Bind("_chunkData", _chunkDatas.GetBuffer());
    }

    // Sync CellHeightRanges to GPU
    {
        _cellHeightRanges.SetDebugName("CellHeightRangeBuffer");
        _cellHeightRanges.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);
        _cellHeightRanges.SyncToGPU(_renderer);

        _cullingPassDescriptorSet.Bind("_heightRanges"_h, _cellHeightRanges.GetBuffer());
    }
}

void TerrainRenderer::Draw(const RenderResources& resources, u8 frameIndex, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList, const DrawParams& params)
{
    Renderer::GraphicsPipelineDesc pipelineDesc;
    graphResources.InitializePipelineDesc(pipelineDesc);

    // Shaders
    Renderer::VertexShaderDesc vertexShaderDesc;
    vertexShaderDesc.path = "Terrain/Draw.vs.hlsl";
    vertexShaderDesc.AddPermutationField("EDITOR_PASS", "0");
    vertexShaderDesc.AddPermutationField("SHADOW_PASS", params.shadowPass ? "1" : "0");

    pipelineDesc.states.vertexShader = _renderer->LoadShader(vertexShaderDesc);

    if (!params.shadowPass)
    {
        Renderer::PixelShaderDesc pixelShaderDesc;
        pixelShaderDesc.path = "Terrain/Draw.ps.hlsl";
        pipelineDesc.states.pixelShader = _renderer->LoadShader(pixelShaderDesc);
    }

    // Depth state
    pipelineDesc.states.depthStencilState.depthEnable = true;
    pipelineDesc.states.depthStencilState.depthWriteEnable = true;
    pipelineDesc.states.depthStencilState.depthFunc = Renderer::ComparisonFunc::GREATER;

    // Rasterizer state
    pipelineDesc.states.rasterizerState.cullMode = params.shadowPass ? Renderer::CullMode::NONE : Renderer::CullMode::BACK;
    pipelineDesc.states.rasterizerState.frontFaceMode = Renderer::Settings::FRONT_FACE_STATE;
    pipelineDesc.states.rasterizerState.depthBiasEnabled = params.shadowPass;
    pipelineDesc.states.rasterizerState.depthClampEnabled = params.shadowPass;

    // Render targets
    if (!params.shadowPass)
    {
        pipelineDesc.renderTargets[0] = params.visibilityBuffer;
    }
    pipelineDesc.depthStencil = params.depth;

    // Set pipeline
    Renderer::GraphicsPipelineID pipeline = _renderer->CreatePipeline(pipelineDesc); // This will compile the pipeline and return the ID, or just return ID of cached pipeline
    commandList.BeginPipeline(pipeline);

    // Set index buffer
    commandList.SetIndexBuffer(_cellIndices.GetBuffer(), Renderer::IndexFormat::UInt16);

    if (params.shadowPass)
    {
        struct PushConstants
        {
            u32 cascadeIndex;
        };

        PushConstants* constants = graphResources.FrameNew<PushConstants>();

        constants->cascadeIndex = params.shadowCascade;
        commandList.PushConstant(constants, 0, sizeof(PushConstants));
    }

    // Bind descriptors
    _geometryPassDescriptorSet.Bind("_instanceDatas"_h, params.instanceBuffer);

    // Bind descriptorset
    commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::GLOBAL, &resources.globalDescriptorSet, frameIndex);
    //commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::SHADOWS, &resources.shadowDescriptorSet, frameIndex);
    commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::TERRAIN, &_geometryPassDescriptorSet, frameIndex);

    if (params.cullingEnabled)
    {
        commandList.DrawIndexedIndirect(params.argumentBuffer, params.argumentsIndex * sizeof(Renderer::IndexedIndirectDraw), 1);
    }
    else
    {
        u32 cellCount = static_cast<u32>(_cellDatas.Size());
        TracyPlot("Cell Instance Count", (i64)cellCount);
        commandList.DrawIndexed(Terrain::CELL_NUM_INDICES, cellCount, 0, 0, 0);
    }

    commandList.EndPipeline(pipeline);
}
