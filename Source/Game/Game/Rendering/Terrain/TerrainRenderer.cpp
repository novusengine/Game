#include "TerrainRenderer.h"
#include "SimplexNoise.h"
#include "Game/Rendering/GameRenderer.h"
#include "Game/Rendering/RenderResources.h"
#include "Game/Rendering/Debug/DebugRenderer.h"
#include "Game/Util/ServiceLocator.h"

#include <Base/Util/Timer.h>
#include <Base/CVarSystem/CVarSystem.h>
#include <Input/InputManager.h>
#include <Renderer/Renderer.h>
#include <Renderer/RenderGraph.h>
#include <Renderer/Descriptors/ImageDesc.h>

#include <imgui/imgui.h>

AutoCVar_Int CVAR_TerrainRendererEnabled("terrainRenderer.enabled", "enable terrainrendering", 0, CVarFlags::EditCheckbox);

TerrainRenderer::TerrainRenderer(Renderer::Renderer* renderer, DebugRenderer* debugRenderer)
    : _renderer(renderer)
    , _debugRenderer(debugRenderer)
    , _chunk(ivec3(0, 0, 0))
    //, _chunk2(ivec3(1, 0, 0))
{
    CreatePermanentResources();

    InputManager* inputManager = ServiceLocator::GetGameRenderer()->GetInputManager();
    KeybindGroup* keybindGroup = inputManager->GetKeybindGroupByHash("Debug"_h);

    keybindGroup->AddKeyboardCallback("ToggleWireframe", GLFW_KEY_F1, KeybindAction::Press, KeybindModifier::Any, [this](i32 key, KeybindAction action, KeybindModifier modifier)
    {
        _showWireFrame = !_showWireFrame;

        return true;
    });

    keybindGroup->AddKeyboardCallback("ToggleDebug", GLFW_KEY_F2, KeybindAction::Press, KeybindModifier::Any, [this](i32 key, KeybindAction action, KeybindModifier modifier)
    {
        _debugMode = !_debugMode;
        DebugHandler::Print("DebugMode: %s", (_debugMode) ? "On" : "Off");

        return true;
    });

    keybindGroup->AddKeyboardCallback("ToggleVoxelPoints", GLFW_KEY_F3, KeybindAction::Press, KeybindModifier::Any, [this](i32 key, KeybindAction action, KeybindModifier modifier)
    {
        _showVoxelPoints = !_showVoxelPoints;

        return true;
    });

    keybindGroup->AddKeyboardCallback("IncreaseSharpness", GLFW_KEY_PAGE_UP, KeybindAction::Press, KeybindModifier::Any, [this](i32 key, KeybindAction action, KeybindModifier modifier)
    {
        _sharpness = Math::Min(_sharpness + 1.0f, 64.0f);
        DebugHandler::Print("Sharpness: %.2f", _sharpness);

        return true;
    });

    keybindGroup->AddKeyboardCallback("DecreaseSharpness", GLFW_KEY_PAGE_DOWN, KeybindAction::Press, KeybindModifier::Any, [this](i32 key, KeybindAction action, KeybindModifier modifier)
    {
        _sharpness = Math::Max(_sharpness - 1.0f, 1.0f);
        DebugHandler::Print("Sharpness: %.2f", _sharpness);

        return true;
    });
}

TerrainRenderer::~TerrainRenderer()
{

}

void TerrainRenderer::Update(f32 deltaTime)
{
    ZoneScoped;

    if (!CVAR_TerrainRendererEnabled.Get())
        return;

    // Terrain Generation
    if (ImGui::Begin("Terrain Generation"))
    {
        ImGui::Text("Settings");
        ImGui::Separator();

        if (ImGui::InputInt("Octaves", &_octaves))
        {
            _octaves = Math::Clamp(_octaves, 0, 16);
        }

        if (ImGui::InputFloat("Frequency", &_frequency))
        {
            _frequency = Math::Max(_frequency, 0.00001f);
        }

        if (ImGui::InputFloat("Amplitude", &_amplitude))
        {
            _amplitude = Math::Max(_amplitude, 0.00001f);
        }

        if (ImGui::InputFloat("Height Sample Distance", &_heightSampleDistance))
        {
            _heightSampleDistance = Math::Max(_heightSampleDistance, 0.00001f);
        }

        if (ImGui::InputInt("Height Steps", &_heightSteps))
        {
            _heightSteps = Math::Clamp(_heightSteps, 1, 16);
        }

        ImGui::Separator();
        ImGui::Text("Thresholds");

        if (ImGui::InputFloat("Stone", &_stoneThreshold))
        {
            _stoneThreshold = Math::Max(_stoneThreshold, 0.00001f);
        }

        if (ImGui::InputFloat("Stone Dirt Mix", &_stoneDirtMixThreshold))
        {
            _stoneDirtMixThreshold = Math::Max(_stoneDirtMixThreshold, 0.00001f);
        }

        if (ImGui::InputFloat("Dirt", &_dirtThreshold))
        {
            _dirtThreshold = Math::Max(_dirtThreshold, 0.00001f);
        }

        if (ImGui::InputFloat("Dirt Grass Mix", &_dirtGrassMixThreshold))
        {
            _dirtGrassMixThreshold = Math::Max(_dirtGrassMixThreshold, 0.00001f);
        }

        if (ImGui::Button("Generate"))
        {
            Clear();

            GenerateVoxels(_chunk);
            //GenerateVoxels(_chunk2);

            DebugHandler::Print("Vertices: %u, Indices: %u", _vertexPositions.Size(), _indices.Size());
        }
    }
    ImGui::End();

    // Debug draw the voxel points
    if (_showVoxelPoints)
    {
        f32 maxVoxelValue = 0.0f;
        auto& voxels = _chunk.GetVoxels();
        auto& voxelMaterials = _chunk.GetVoxelMaterials();
        for (u32 i = 0; i < voxels.size(); i++)
        {
            f32 voxelValue = voxels[i];
            if (voxelValue > 0.5f && voxelValue < 1.5f)
            {
                ivec3 coord = TerrainUtils::VoxelIndexToCoord(i);
                maxVoxelValue = Math::Max(maxVoxelValue, voxelValue);

                u32 material = voxelMaterials[i];

                uvec3 colorValues = uvec3(0,0,0);
                colorValues[material] = 255;

                u32 color = (colorValues[0] & 0xFF);
                color |= (colorValues[1] & 0xFF) << 8;
                color |= (colorValues[2] & 0xFF) << 16;

                vec3 pos = vec3(coord) - (TerrainUtils::TERRAIN_CHUNK_SIZE / 2.0f);
                _debugRenderer->DrawAABB3D(pos, vec3(0.1f), color);
            }
        }

        _maxVoxelValue = maxVoxelValue;
    }

    SyncToGPU();
}

void TerrainRenderer::Clear()
{
    ZoneScoped;

    _vertexPositions.Clear();
    _vertexNormals.Clear();
    _vertexMaterials.Clear();
    _indices.Clear();

    _chunkDatas.Clear();
    _meshletDatas.Clear();
}

void TerrainRenderer::AddCullingPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex)
{
    ZoneScoped;

    if (!CVAR_TerrainRendererEnabled.Get())
        return;

    size_t numChunks = _chunkDatas.Size();
    if (numChunks == 0)
        return;

    struct Data{};
    renderGraph->AddPass<Data>("TerrainCulling",
        [=](Data& data, Renderer::RenderGraphBuilder& builder) // Setup
        {
            return true; // Return true from setup to enable this pass, return false to disable it
        },
        [=](Data& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList) // Execute
        {
            GPU_SCOPED_PROFILER_ZONE(commandList, TerrainCulling);

            Renderer::ComputePipelineDesc pipelineDesc;
            graphResources.InitializePipelineDesc(pipelineDesc);

            Renderer::ComputeShaderDesc shaderDesc;

            // Cull Chunks
            {
                shaderDesc.path = "Terrain/CullChunks.cs.hlsl";
                pipelineDesc.computeShader = _renderer->LoadShader(shaderDesc);

                commandList.FillBuffer(_survivedMeshletCountBuffer, 0, 4, 0);
                commandList.FillBuffer(_indirectDrawArgumentBuffer, 0, 4, 0);
                commandList.FillBuffer(_indirectDispatchArgumentBuffer, 0, 8, 0);
                commandList.PipelineBarrier(Renderer::PipelineBarrierType::TransferDestToComputeShaderRW, _indirectDispatchArgumentBuffer);

                Renderer::ComputePipelineID activePipeline = _renderer->CreatePipeline(pipelineDesc);
                commandList.BeginPipeline(activePipeline);

                commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::PER_DRAW, &_cullDescriptorSet, frameIndex);

                struct Constants
                {
                    u32 numChunks;
                };

                Constants* constants = graphResources.FrameNew<Constants>();
                constants->numChunks = static_cast<u32>(numChunks);
                commandList.PushConstant(constants, 0, sizeof(Constants));

                u32 dispatchCount = Renderer::GetDispatchCount(static_cast<u32>(numChunks), 64);
                commandList.Dispatch(dispatchCount, 1, 1);
                commandList.EndPipeline(activePipeline);
            }
            
            commandList.PipelineBarrier(Renderer::PipelineBarrierType::TransferDestToComputeShaderRW, _indirectDrawArgumentBuffer);
            commandList.PipelineBarrier(Renderer::PipelineBarrierType::TransferDestToComputeShaderRW, _survivedMeshletCountBuffer);
            commandList.PipelineBarrier(Renderer::PipelineBarrierType::ComputeWriteToIndirectArguments, _indirectDispatchArgumentBuffer);
            commandList.PipelineBarrier(Renderer::PipelineBarrierType::ComputeWriteToComputeShaderRead, _survivedChunkDataBuffer);

            // Cull Meshlets
            {
                shaderDesc.path = "Terrain/CullMeshlets.cs.hlsl";
                pipelineDesc.computeShader = _renderer->LoadShader(shaderDesc);

                Renderer::ComputePipelineID activePipeline = _renderer->CreatePipeline(pipelineDesc);
                commandList.BeginPipeline(activePipeline);

                commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::PER_DRAW, &_cullDescriptorSet, frameIndex);

                commandList.DispatchIndirect(_indirectDispatchArgumentBuffer, 4);

                commandList.EndPipeline(activePipeline);
            }
        });
}

void TerrainRenderer::AddGeometryPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex)
{
    ZoneScoped;
    
    if (!CVAR_TerrainRendererEnabled.Get())
        return;

    size_t numMeshlets = _meshletDatas.Size();
    if (numMeshlets == 0)
    {
        return;
    }

	struct Data
	{
        Renderer::RenderPassMutableResource color;
        Renderer::RenderPassMutableResource depth;
	};
    renderGraph->AddPass<Data>("TerrainGeometry",
        [=, &resources](Data& data, Renderer::RenderGraphBuilder& builder) // Setup
        {
            data.color = builder.Write(resources.finalColor, Renderer::RenderGraphBuilder::WriteMode::RENDERTARGET, Renderer::RenderGraphBuilder::LoadMode::LOAD);
            data.depth = builder.Write(resources.depth, Renderer::RenderGraphBuilder::WriteMode::RENDERTARGET, Renderer::RenderGraphBuilder::LoadMode::LOAD);

            return true; // Return true from setup to enable this pass, return false to disable it
        },
        [=, &resources](Data& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList) // Execute
        {
            GPU_SCOPED_PROFILER_ZONE(commandList, TerrainGeometry);

            Renderer::GraphicsPipelineDesc pipelineDesc;
            graphResources.InitializePipelineDesc(pipelineDesc);

            // Rasterizer state
            pipelineDesc.states.rasterizerState.cullMode = Renderer::CullMode::BACK;
            pipelineDesc.states.rasterizerState.frontFaceMode = Renderer::FrontFaceState::COUNTERCLOCKWISE;

            // Depth stencil
            pipelineDesc.depthStencil = data.depth;

            pipelineDesc.states.depthStencilState.depthEnable = true;
            pipelineDesc.states.depthStencilState.depthWriteEnable = true;
            pipelineDesc.states.depthStencilState.depthFunc = Renderer::ComparisonFunc::GREATER;

            // Render targets
            pipelineDesc.renderTargets[0] = data.color;

            // Shaders
            Renderer::VertexShaderDesc vertexShaderDesc;
            vertexShaderDesc.path = "Terrain/Draw.vs.hlsl";
            vertexShaderDesc.AddPermutationField("WIRE_FRAME", "0");
            pipelineDesc.states.vertexShader = _renderer->LoadShader(vertexShaderDesc);

            Renderer::PixelShaderDesc pixelShaderDesc;
            pixelShaderDesc.path = "Terrain/Draw.ps.hlsl";
            pixelShaderDesc.AddPermutationField("WIRE_FRAME", "0");
            pixelShaderDesc.AddPermutationField("DEBUG_MODE", std::to_string(_debugMode));
            pipelineDesc.states.pixelShader = _renderer->LoadShader(pixelShaderDesc);

            Renderer::GraphicsPipelineID pipeline = _renderer->CreatePipeline(pipelineDesc);

            // Set viewport
            vec2 renderTargetSize = _renderer->GetImageDimension(resources.finalColor);

            commandList.SetViewport(0, 0, renderTargetSize.x, renderTargetSize.y, 0.0f, 1.0f);
            commandList.SetScissorRect(0, static_cast<u32>(renderTargetSize.x), 0, static_cast<u32>(renderTargetSize.y));

            commandList.ImageBarrier(resources.finalColor);

            commandList.PipelineBarrier(Renderer::PipelineBarrierType::ComputeWriteToIndirectArguments, _indirectDrawArgumentBuffer);
            commandList.PipelineBarrier(Renderer::PipelineBarrierType::ComputeWriteToVertexShaderRead, _culledIndexBuffer);
            commandList.PipelineBarrier(Renderer::PipelineBarrierType::ComputeWriteToIndexBuffer, _culledIndexBuffer);
            commandList.PipelineBarrier(Renderer::PipelineBarrierType::ComputeWriteToVertexShaderRead, _survivedMeshletBuffer);

            commandList.BeginPipeline(pipeline);
            {
                struct Constants
                {
                    float triplanarSharpness;
                };
                Constants* constants = graphResources.FrameNew<Constants>();
                constants->triplanarSharpness = _sharpness;
                commandList.PushConstant(constants, 0, sizeof(Constants));

                commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::GLOBAL, &resources.globalDescriptorSet, frameIndex);
                commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::PER_DRAW, &_geometryDescriptorSet, frameIndex);

                commandList.SetIndexBuffer(_culledIndexBuffer, Renderer::IndexFormat::UInt32);
                commandList.DrawIndexedIndirect(_indirectDrawArgumentBuffer, 0, 1);
            }
            commandList.EndPipeline(pipeline);

            // Wireframe
            if (_showWireFrame)
            {
                Renderer::VertexShaderDesc wireFrameVertexShaderDesc;
                wireFrameVertexShaderDesc.path = "Terrain/Draw.vs.hlsl";
                wireFrameVertexShaderDesc.AddPermutationField("WIRE_FRAME", "1");
                pipelineDesc.states.vertexShader = _renderer->LoadShader(wireFrameVertexShaderDesc);

                Renderer::PixelShaderDesc wireFramePixelShaderDesc;
                wireFramePixelShaderDesc.path = "Terrain/Draw.ps.hlsl";
                wireFramePixelShaderDesc.AddPermutationField("WIRE_FRAME", "1");
                wireFramePixelShaderDesc.AddPermutationField("DEBUG_MODE", "0");
                pipelineDesc.states.pixelShader = _renderer->LoadShader(wireFramePixelShaderDesc);

                pipelineDesc.states.rasterizerState.fillMode = Renderer::FillMode::WIREFRAME;
                pipeline = _renderer->CreatePipeline(pipelineDesc);

                commandList.BeginPipeline(pipeline);
                {
                    commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::GLOBAL, &resources.globalDescriptorSet, frameIndex);
                    commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::PER_DRAW, &_geometryDescriptorSet, frameIndex);

                    commandList.SetIndexBuffer(_culledIndexBuffer, Renderer::IndexFormat::UInt32);
                    commandList.DrawIndexedIndirect(_indirectDrawArgumentBuffer, 0, 1);
                }
                commandList.EndPipeline(pipeline);
            }
        });
}

void TerrainRenderer::GenerateVoxels(Chunk& chunk)
{
    ZoneScoped;

    Timer timer;

    auto& voxels = chunk.GetVoxels();
    auto& voxelMaterials = chunk.GetVoxelMaterials();

    for (u32 i = 0; i < voxels.size(); i++)
    {
        ivec3 coord = TerrainUtils::VoxelIndexToCoord(i);

        voxels[i] = GenerateVoxel(chunk.GetChunk3DIndex(), coord, voxelMaterials[i]);
    }
    
    f32 time = timer.GetDeltaTime();
    DebugHandler::Print("Generating voxels: %fs", time);
    
    chunk.Meshify(0.5f, _vertexPositions, _vertexNormals, _indices, _meshletDatas, _vertexMaterials,  _chunkDatas, _materials.Size());

    time = timer.GetDeltaTime();
    DebugHandler::Print("Total: %fs", time);
}

f32 TerrainRenderer::GenerateVoxel(ivec3 chunkCoord, ivec3 voxelCoord, u8& materialID)
{
    ZoneScoped;

    vec3 chunkOffset = vec3(chunkCoord * (TerrainUtils::TERRAIN_CHUNK_NUM_VOXELS - i32(TerrainUtils::TERRAIN_VOXEL_BORDER + 1)));
    chunkOffset -= vec3(TerrainUtils::TERRAIN_CHUNK_NUM_VOXELS / 2); // Put center in middle of chunk
    
    vec3 noisePos = (vec3(voxelCoord) + chunkOffset);

    f32 surfaceLevel = GetSurfaceLevel(noisePos);

    vec3 directions[] = {
        vec3(-1.0f, 0.0f, 0.0f),
        vec3(1.0f, 0.0f, 0.0f),
        vec3(0.0f, 0.0f, -1.0f),
        vec3(0.0f, 0.0f, 1.0f),
        glm::normalize(vec3(1.0f, 0.0f, 1.0f)),
        glm::normalize(vec3(-1.0f, 0.0f, -1.0f)),
        glm::normalize(vec3(1.0f, 0.0f, -1.0f)),
        glm::normalize(vec3(-1.0f, 0.0f, 1.0f))
    };
    u32 numDirections = sizeof(directions) / sizeof(vec3);

    f32 maxHeightDiff = 0.0f;
    for (u32 i = 0; i < numDirections; i++)
    {
        for (i32 step = 0; step < _heightSteps; step++)
        {
            f32 sampleDistance = _heightSampleDistance * (step+1);

            f32 dirSurfaceLevelDiff = GetSurfaceLevel(noisePos - (directions[i] * sampleDistance)) - surfaceLevel;//voxelCoord.y;
            maxHeightDiff = Math::Max(maxHeightDiff, Math::Abs(dirSurfaceLevelDiff));
        }
    }
    
    if (maxHeightDiff > _stoneThreshold)
    {
        materialID = 2; // Stone
    }
    else if (maxHeightDiff > _dirtThreshold)
    {
        materialID = 1; // Dirt
    }
    else
    {
        materialID = 0; // Grass
    }

    f32 diff = noisePos.y - surfaceLevel;
    return diff;

    // Sphere, keep this code here for future debugging purposes
    /*vec3 chunkCenter = vec3(TerrainUtils::TERRAIN_CHUNK_NUM_VOXELS_X, TerrainUtils::TERRAIN_CHUNK_NUM_VOXELS_Y, TerrainUtils::TERRAIN_CHUNK_NUM_VOXELS_Z) / 2.0f;
    vec3 pos = voxelCoord;

    const f32 radius = 50.0f;
    f32 distance = glm::distance(chunkCenter, pos) / radius;

    materialID = 3;
    return distance;*/
}

f32 TerrainRenderer::GetSurfaceLevel(vec3 noisePos)
{
    ZoneScoped;

    f32 surfaceLevel = 0.0f;
    for (i32 i = 0; i < _octaves; i++)
    {
        vec3 octavePos = noisePos * (_frequency * (i + 1));

        /*if (i == 0)
        {
            f32 rotationX = SimplexNoise::noise(octavePos.x) * 2.0f;
            f32 rotationY = SimplexNoise::noise(octavePos.y) * 2.0f;
            f32 rotationZ = SimplexNoise::noise(octavePos.z) * 2.0f;

            octavePos = vec3(glm::rotate(quat(vec3(rotationX, rotationY, rotationZ)), vec4(octavePos, 1.0f)));
        }*/

        vec3 warpPos = octavePos * 0.004f;
        f32 warpX = SimplexNoise::noise(warpPos.x) * 8.0f;
        f32 warpY = SimplexNoise::noise(warpPos.y) * 8.0f;
        f32 warpZ = SimplexNoise::noise(warpPos.z) * 8.0f;

        vec3 warp = vec3(warpX, warpY, warpZ);
        octavePos += warp;

        surfaceLevel += SimplexNoise::noise(octavePos.x, octavePos.z) * (_amplitude / (i + 1));
    }

    return surfaceLevel;
}

void TerrainRenderer::CreatePermanentResources()
{
    ZoneScoped;

    Renderer::TextureArrayDesc textureArrayDesc;
    textureArrayDesc.size = 512;

    _terrainTextures = _renderer->CreateTextureArray(textureArrayDesc);
    _geometryDescriptorSet.Bind("_textures", _terrainTextures);

    // Load grass textures
    {
        Renderer::TextureDesc textureDesc;
        textureDesc.path = "Data/textures/grass1.jpg";

        u32 arrayIndex;
        _renderer->LoadTextureIntoArray(textureDesc, _terrainTextures, arrayIndex);
    }

    {
        Renderer::TextureDesc textureDesc;
        textureDesc.path = "Data/textures/grass1_n.jpg";

        u32 arrayIndex;
        _renderer->LoadTextureIntoArray(textureDesc, _terrainTextures, arrayIndex);
    }

    {
        Renderer::TextureDesc textureDesc;
        textureDesc.path = "Data/textures/grass1_h.jpg";

        u32 arrayIndex;
        _renderer->LoadTextureIntoArray(textureDesc, _terrainTextures, arrayIndex);
    }

    // Load dirt textures
    {
        Renderer::TextureDesc textureDesc;
        textureDesc.path = "Data/textures/dirt1.jpg";

        u32 arrayIndex;
        _renderer->LoadTextureIntoArray(textureDesc, _terrainTextures, arrayIndex);
    }

    {
        Renderer::TextureDesc textureDesc;
        textureDesc.path = "Data/textures/dirt1_n.jpg";

        u32 arrayIndex;
        _renderer->LoadTextureIntoArray(textureDesc, _terrainTextures, arrayIndex);
    }

    {
        Renderer::TextureDesc textureDesc;
        textureDesc.path = "Data/textures/dirt1_h.jpg";

        u32 arrayIndex;
        _renderer->LoadTextureIntoArray(textureDesc, _terrainTextures, arrayIndex);
    }

    // Load stone textures
    {
        Renderer::TextureDesc textureDesc;
        textureDesc.path = "Data/textures/stone1.jpg";

        u32 arrayIndex;
        _renderer->LoadTextureIntoArray(textureDesc, _terrainTextures, arrayIndex);
    }

    {
        Renderer::TextureDesc textureDesc;
        textureDesc.path = "Data/textures/stone1_n.jpg";

        u32 arrayIndex;
        _renderer->LoadTextureIntoArray(textureDesc, _terrainTextures, arrayIndex);
    }

    {
        Renderer::TextureDesc textureDesc;
        textureDesc.path = "Data/textures/stone1_h.jpg";

        u32 arrayIndex;
        _renderer->LoadTextureIntoArray(textureDesc, _terrainTextures, arrayIndex);
    }

    // Hardcoded material "loading" for now
    _materials.WriteLock([&](std::vector<TerrainMaterial>& materials)
    {
        // Grass
        TerrainMaterial& grassMaterial = materials.emplace_back();
        grassMaterial.xTextureID = 0;
        grassMaterial.yTextureID = 0;
        grassMaterial.zTextureID = 0;

        // Dirt
        TerrainMaterial& dirtMaterial = materials.emplace_back();
        dirtMaterial.xTextureID = 3;
        dirtMaterial.yTextureID = 3;
        dirtMaterial.zTextureID = 3;

        // Stone
        TerrainMaterial& stoneMaterial = materials.emplace_back();
        stoneMaterial.xTextureID = 6;
        stoneMaterial.yTextureID = 6;
        stoneMaterial.zTextureID = 6;
    });

    if (CVAR_TerrainRendererEnabled.Get())
    {
        GenerateVoxels(_chunk);
        //GenerateVoxels(_chunk2);

        DebugHandler::Print("Meshlets: %u, Vertices: %u, Indices: %u", _meshletDatas.Size(), _vertexPositions.Size(), _indices.Size());
    }

    // Sampler
    Renderer::SamplerDesc samplerDesc;
    samplerDesc.enabled = true;
    samplerDesc.filter = Renderer::SamplerFilter::ANISOTROPIC;
    samplerDesc.maxAnisotropy = 8;
    samplerDesc.addressU = Renderer::TextureAddressMode::WRAP;
    samplerDesc.addressV = Renderer::TextureAddressMode::WRAP;
    samplerDesc.addressW = Renderer::TextureAddressMode::CLAMP;
    samplerDesc.shaderVisibility = Renderer::ShaderVisibility::PIXEL;

    _linearSampler = _renderer->CreateSampler(samplerDesc);
    _geometryDescriptorSet.Bind("_sampler"_h, _linearSampler);

    _vertexPositions.SetDebugName("TerrainVertexPositions");
    _vertexPositions.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);
    if (_vertexPositions.SyncToGPU(_renderer))
    {
        _geometryDescriptorSet.Bind("_vertexPositions", _vertexPositions.GetBuffer());
    }

    _vertexNormals.SetDebugName("TerrainVertexNormals");
    _vertexNormals.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);
    if (_vertexNormals.SyncToGPU(_renderer))
    {
        _geometryDescriptorSet.Bind("_vertexNormals", _vertexNormals.GetBuffer());
    }

    _vertexMaterials.SetDebugName("TerrainVertexMaterials");
    _vertexMaterials.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);
    if (_vertexMaterials.SyncToGPU(_renderer))
    {
        _geometryDescriptorSet.Bind("_vertexMaterials", _vertexMaterials.GetBuffer());
    }

    _indices.SetDebugName("TerrainIndices");
    _indices.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);
    if (_indices.SyncToGPU(_renderer))
    {
        _geometryDescriptorSet.Bind("_indices", _indices.GetBuffer());
    }

    // Culled Terrain indices
    {
        Renderer::BufferDesc bufferDesc;
        bufferDesc.name = "CulledTerrainIndices";
        bufferDesc.usage = Renderer::BufferUsage::INDEX_BUFFER | Renderer::BufferUsage::STORAGE_BUFFER;
        bufferDesc.size = _indices.Size() * sizeof(u32);

        _culledIndexBuffer = _renderer->CreateBuffer(_culledIndexBuffer, bufferDesc);

        _cullDescriptorSet.Bind("_culledIndices", _culledIndexBuffer);
    }

    _materials.SetDebugName("TerrainMaterials");
    _materials.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);
    if (_materials.SyncToGPU(_renderer))
    {
        _geometryDescriptorSet.Bind("_materials", _materials.GetBuffer());
    }

    _meshletDatas.SetDebugName("TerrainMeshlets");
    _meshletDatas.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);
    if (_meshletDatas.SyncToGPU(_renderer))
    {
        _geometryDescriptorSet.Bind("_meshletDatas", _meshletDatas.GetBuffer());
        _cullDescriptorSet.Bind("_meshletDatas", _meshletDatas.GetBuffer());
    }

    _chunkDatas.SetDebugName("TerrainChunkDatas");
    _chunkDatas.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);
    if (_chunkDatas.SyncToGPU(_renderer))
    {
        _geometryDescriptorSet.Bind("_chunkDatas", _chunkDatas.GetBuffer());
        _cullDescriptorSet.Bind("_chunkDatas", _chunkDatas.GetBuffer());
    }

    // Indirect argument buffers
    {
        Renderer::BufferDesc bufferDesc;
        bufferDesc.name = "IndirectDrawArgumentBuffer";
        bufferDesc.usage = Renderer::BufferUsage::INDIRECT_ARGUMENT_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION | Renderer::BufferUsage::STORAGE_BUFFER;
        bufferDesc.size = sizeof(DrawCall);

        _indirectDrawArgumentBuffer = _renderer->CreateBuffer(_indirectDrawArgumentBuffer, bufferDesc);
        _cullDescriptorSet.Bind("_drawArguments", _indirectDrawArgumentBuffer);

        // Set InstanceCount to 1
        auto uploadBuffer = _renderer->CreateUploadBuffer(_indirectDrawArgumentBuffer, 0, bufferDesc.size);
        memset(uploadBuffer->mappedMemory, 0, bufferDesc.size);
        static_cast<u32*>(uploadBuffer->mappedMemory)[1] = 1;
    }

    {
        Renderer::BufferDesc bufferDesc;
        bufferDesc.name = "IndirectDispatchArgumentBuffer";
        bufferDesc.usage = Renderer::BufferUsage::INDIRECT_ARGUMENT_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION | Renderer::BufferUsage::STORAGE_BUFFER;
        bufferDesc.size = sizeof(PaddedDispatch);

        _indirectDispatchArgumentBuffer = _renderer->CreateBuffer(_indirectDispatchArgumentBuffer, bufferDesc);
        _cullDescriptorSet.Bind("_dispatchArguments", _indirectDispatchArgumentBuffer);

        // Set y, z to 1
        auto uploadBuffer = _renderer->CreateUploadBuffer(_indirectDispatchArgumentBuffer, 0, bufferDesc.size);
        memset(uploadBuffer->mappedMemory, 0, bufferDesc.size);
        static_cast<u32*>(uploadBuffer->mappedMemory)[2] = 1; // Y
        static_cast<u32*>(uploadBuffer->mappedMemory)[3] = 1; // Z
    }

    {
        Renderer::BufferDesc bufferDesc;
        bufferDesc.name = "SurvivedMeshletDataBuffer";
        bufferDesc.usage = Renderer::BufferUsage::STORAGE_BUFFER;
        bufferDesc.size = sizeof(SurvivedMeshletData) * _meshletDatas.Size();

        _survivedMeshletBuffer = _renderer->CreateBuffer(_survivedMeshletBuffer, bufferDesc);
        _cullDescriptorSet.Bind("_survivedMeshletDatas", _survivedMeshletBuffer);
        _geometryDescriptorSet.Bind("_survivedMeshletDatas", _survivedMeshletBuffer);
    }

    {
        Renderer::BufferDesc bufferDesc;
        bufferDesc.name = "SurvivedMeshletDataCountBuffer";
        bufferDesc.usage = Renderer::BufferUsage::TRANSFER_DESTINATION | Renderer::BufferUsage::STORAGE_BUFFER;
        bufferDesc.size = sizeof(u32);

        _survivedMeshletCountBuffer = _renderer->CreateBuffer(_survivedMeshletCountBuffer, bufferDesc);
        _cullDescriptorSet.Bind("_meshletChunkCount", _survivedMeshletCountBuffer);
    }

    {
        Renderer::BufferDesc bufferDesc;
        bufferDesc.name = "SurvivedChunkDataBuffer";
        bufferDesc.usage = Renderer::BufferUsage::TRANSFER_DESTINATION | Renderer::BufferUsage::STORAGE_BUFFER;
        bufferDesc.size = sizeof(SurvivedChunkData) * _chunkDatas.Size();

        _survivedChunkDataBuffer = _renderer->CreateBuffer(_survivedChunkDataBuffer, bufferDesc);
        _geometryDescriptorSet.Bind("_survivedChunkDatas", _survivedChunkDataBuffer);
        _cullDescriptorSet.Bind("_survivedChunkDatas", _survivedChunkDataBuffer);
    }
}

void TerrainRenderer::SyncToGPU()
{
    if (_vertexPositions.SyncToGPU(_renderer))
    {
        _geometryDescriptorSet.Bind("_vertexPositions", _vertexPositions.GetBuffer());
    }

    if (_vertexNormals.SyncToGPU(_renderer))
    {
        _geometryDescriptorSet.Bind("_vertexNormals", _vertexNormals.GetBuffer());
    }

    if (_vertexMaterials.SyncToGPU(_renderer))
    {
        _geometryDescriptorSet.Bind("_vertexMaterials", _vertexMaterials.GetBuffer());
    }

    if (_indices.SyncToGPU(_renderer))
    {
        _geometryDescriptorSet.Bind("_indices", _indices.GetBuffer());

        // Culled Terrain indices
        {
            Renderer::BufferDesc bufferDesc;
            bufferDesc.name = "CulledTerrainIndices";
            bufferDesc.usage = Renderer::BufferUsage::INDEX_BUFFER | Renderer::BufferUsage::STORAGE_BUFFER;
            bufferDesc.size = _indices.Size() * sizeof(u32);

            _culledIndexBuffer = _renderer->CreateBuffer(_culledIndexBuffer, bufferDesc);

            _cullDescriptorSet.Bind("_culledIndices", _culledIndexBuffer);
        }
    }

    if (_chunkDatas.SyncToGPU(_renderer))
    {
        _geometryDescriptorSet.Bind("_chunkDatas", _chunkDatas.GetBuffer());
        _cullDescriptorSet.Bind("_chunkDatas", _chunkDatas.GetBuffer());

        // Survived Chunk Datas buffer
        {
            Renderer::BufferDesc bufferDesc;
            bufferDesc.name = "SurvivedChunkDataBuffer";
            bufferDesc.usage = Renderer::BufferUsage::TRANSFER_DESTINATION | Renderer::BufferUsage::STORAGE_BUFFER;
            bufferDesc.size = sizeof(SurvivedChunkData) * _chunkDatas.Size();

            _survivedChunkDataBuffer = _renderer->CreateBuffer(_survivedChunkDataBuffer, bufferDesc);
            _geometryDescriptorSet.Bind("_survivedChunkDatas", _survivedChunkDataBuffer);
            _cullDescriptorSet.Bind("_survivedChunkDatas", _survivedChunkDataBuffer);
        }
    }

    if (_meshletDatas.SyncToGPU(_renderer))
    {
        _geometryDescriptorSet.Bind("_meshletDatas", _meshletDatas.GetBuffer());
        _cullDescriptorSet.Bind("_meshletDatas", _meshletDatas.GetBuffer());

        // Survived Meshlet buffer
        {
            Renderer::BufferDesc bufferDesc;
            bufferDesc.name = "SurvivedMeshletDataBuffer";
            bufferDesc.usage = Renderer::BufferUsage::STORAGE_BUFFER;
            bufferDesc.size = sizeof(SurvivedMeshletData) * _meshletDatas.Size();

            _survivedMeshletBuffer = _renderer->CreateBuffer(_survivedMeshletBuffer, bufferDesc);
            _cullDescriptorSet.Bind("_survivedMeshletDatas", _survivedMeshletBuffer);
            _geometryDescriptorSet.Bind("_survivedMeshletDatas", _survivedMeshletBuffer);
        }
    }
}
