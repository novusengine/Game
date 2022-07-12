#include "TerrainRenderer.h"
#include "Game/Rendering/RenderResources.h"

#include <Renderer/Renderer.h>
#include <Renderer/RenderGraph.h>
#include <Renderer/Descriptors/ImageDesc.h>

TerrainRenderer::TerrainRenderer(Renderer::Renderer* renderer)
	: _renderer(renderer)
{
    CreatePermanentResources();
}

TerrainRenderer::~TerrainRenderer()
{

}

void TerrainRenderer::Update(f32 deltaTime)
{

}

void TerrainRenderer::AddTriangularizationPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex)
{
    struct Data
    {

    };
    renderGraph->AddPass<Data>("VoxelTriangularization",
        [=](Data& data, Renderer::RenderGraphBuilder& builder) // Setup
        {
            return true; // Return true from setup to enable this pass, return false to disable it
        },
        [=](Data& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList) // Execute
        {
            GPU_SCOPED_PROFILER_ZONE(commandList, TerrainGeometry);

            commandList.FillBuffer(_arguments, 0, 4, 0); // Set vertexCount to 0
            commandList.PipelineBarrier(Renderer::PipelineBarrierType::TransferDestToComputeShaderRW, _arguments);

            Renderer::ComputeShaderDesc shaderDesc;
            shaderDesc.path = "Terrain/marchingCubes.cs.hlsl";

            Renderer::ComputePipelineDesc pipelineDesc;
            pipelineDesc.computeShader = _renderer->LoadShader(shaderDesc);

            Renderer::ComputePipelineID pipeline = _renderer->CreatePipeline(pipelineDesc);

            commandList.BeginPipeline(pipeline);

            commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::PER_DRAW, &_marchingCubeDescriptorSet, frameIndex);

            struct Constants
            {
                f32 target;

                u32 width;
                u32 height;
                u32 depth;
                u32 border;
            };

            Constants* constants = graphResources.FrameNew<Constants>();
            constants->target = 0.5f;
            constants->width = TerrainUtils::TERRAIN_CHUNK_NUM_VOXELS_X;
            constants->height = TerrainUtils::TERRAIN_CHUNK_NUM_VOXELS_Y;
            constants->depth = TerrainUtils::TERRAIN_CHUNK_NUM_VOXELS_Z;
            constants->border = 1;
            commandList.PushConstant(constants, 0, sizeof(Constants));

            ivec3 dispatchSize = ivec3(TerrainUtils::TERRAIN_CHUNK_NUM_VOXELS_X, TerrainUtils::TERRAIN_CHUNK_NUM_VOXELS_Y, TerrainUtils::TERRAIN_CHUNK_NUM_VOXELS_Z) / 4;
            commandList.Dispatch(dispatchSize.x, dispatchSize.y, dispatchSize.z);

            commandList.EndPipeline(pipeline);
        });
}

void TerrainRenderer::AddGeometryPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex)
{
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
            vertexShaderDesc.path = "cube.vs.hlsl";
            pipelineDesc.states.vertexShader = _renderer->LoadShader(vertexShaderDesc);

            Renderer::PixelShaderDesc pixelShaderDesc;
            pixelShaderDesc.path = "cube.ps.hlsl";
            pipelineDesc.states.pixelShader = _renderer->LoadShader(pixelShaderDesc);

            Renderer::GraphicsPipelineID pipeline = _renderer->CreatePipeline(pipelineDesc);

            // Set viewport
            vec2 renderTargetSize = _renderer->GetImageDimension(resources.finalColor);

            commandList.SetViewport(0, 0, renderTargetSize.x, renderTargetSize.y, 0.0f, 1.0f);
            commandList.SetScissorRect(0, static_cast<u32>(renderTargetSize.x), 0, static_cast<u32>(renderTargetSize.y));

            commandList.PipelineBarrier(Renderer::PipelineBarrierType::ComputeWriteToVertexShaderRead, _vertices);
            commandList.PipelineBarrier(Renderer::PipelineBarrierType::ComputeWriteToIndirectArguments, _arguments);

            commandList.BeginPipeline(pipeline);

            commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::GLOBAL, &resources.globalDescriptorSet, frameIndex);
            commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::PER_DRAW, &_geometryDescriptorSet, frameIndex);

            //commandList.DrawIndirect(_arguments, 0, 1);
            commandList.Draw(TerrainUtils::TERRAIN_WORLD_NUM_VOXELS * 3 * 5, 1, 0, 0);
            commandList.EndPipeline(pipeline);
        });
}





f32 GenerateVoxel(int x, int y, int z)
{
    // Sphere
    vec3 chunkCenter = vec3(TerrainUtils::TERRAIN_CHUNK_NUM_VOXELS_X, TerrainUtils::TERRAIN_CHUNK_NUM_VOXELS_Y, TerrainUtils::TERRAIN_CHUNK_NUM_VOXELS_Z) / 2.0f;
    vec3 pos = vec3(x, y, z);

    const f32 radius = 10.0f;
    f32 distance = glm::distance(chunkCenter, pos) / radius;
    return distance;
}

void TerrainRenderer::CreatePermanentResources()
{
    //auto& voxels = chunk.GetVoxels();

    // Sampler
    Renderer::SamplerDesc samplerDesc;
    samplerDesc.enabled = true;
    samplerDesc.filter = Renderer::SamplerFilter::MIN_MAG_MIP_LINEAR;
    samplerDesc.addressU = Renderer::TextureAddressMode::CLAMP;
    samplerDesc.addressV = Renderer::TextureAddressMode::CLAMP;
    samplerDesc.addressW = Renderer::TextureAddressMode::CLAMP;
    samplerDesc.shaderVisibility = Renderer::ShaderVisibility::PIXEL;

    _linearSampler = _renderer->CreateSampler(samplerDesc);
    _geometryDescriptorSet.Bind("_sampler"_h, _linearSampler);

    _triangleConnectionTable.SyncToGPU(_renderer);
    _marchingCubeDescriptorSet.Bind("_triangleConnectionTable", _triangleConnectionTable.GetBuffer());
    
    _voxels.SetDebugName("Voxels");
    _voxels.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);
    _voxels.WriteLock([&](std::vector<f32>& voxels)
    {
        voxels.resize(TerrainUtils::TERRAIN_WORLD_NUM_VOXELS);
        i32 i = 0;
        for (u32 x = 0; x < TerrainUtils::TERRAIN_CHUNK_NUM_VOXELS_X; x++)
        {
            for (u32 y = 0; y < TerrainUtils::TERRAIN_CHUNK_NUM_VOXELS_Y; y++)
            {
                for (u32 z = 0; z < TerrainUtils::TERRAIN_CHUNK_NUM_VOXELS_Z; z++)
                {
                    voxels[i++] = GenerateVoxel(x, y, z);
                }
            }
        }
    });
    _voxels.SyncToGPU(_renderer);
    _marchingCubeDescriptorSet.Bind("_voxels", _voxels.GetBuffer());

    // Argument buffer
    Renderer::BufferDesc bufferDesc;
    bufferDesc.name = "TerrainArguments";
    bufferDesc.size = sizeof(ivec4);
    bufferDesc.cpuAccess = Renderer::BufferCPUAccess::WriteOnly;
    bufferDesc.usage = Renderer::BufferUsage::TRANSFER_DESTINATION | Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::INDIRECT_ARGUMENT_BUFFER;

    _arguments = _renderer->CreateBuffer(bufferDesc);
    ivec4* arguments = static_cast<ivec4*>(_renderer->MapBuffer(_arguments));
    *arguments = ivec4(0, 1, 0, 0);
    _renderer->UnmapBuffer(_arguments);
    _marchingCubeDescriptorSet.Bind("_arguments", _arguments);

    // Vertex buffer
    Renderer::BufferDesc vertexBufferDesc;
    vertexBufferDesc.name = "TerrainVertices";
    vertexBufferDesc.size = sizeof(Vertex) * TerrainUtils::TERRAIN_WORLD_NUM_VOXELS * 3 * 5;
    vertexBufferDesc.cpuAccess = Renderer::BufferCPUAccess::WriteOnly;
    vertexBufferDesc.usage = Renderer::BufferUsage::TRANSFER_DESTINATION | Renderer::BufferUsage::STORAGE_BUFFER;

    _vertices = _renderer->CreateBuffer(vertexBufferDesc);
    _marchingCubeDescriptorSet.Bind("_vertices", _vertices);
    _geometryDescriptorSet.Bind("_vertices", _vertices);
}