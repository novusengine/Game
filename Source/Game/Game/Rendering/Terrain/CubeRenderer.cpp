#include "CubeRenderer.h"
#include "Game/Rendering/RenderResources.h"

#include <Renderer/Renderer.h>
#include <Renderer/RenderGraph.h>
#include <Renderer/Descriptors/ImageDesc.h>

CubeRenderer::CubeRenderer(Renderer::Renderer* renderer)
	: _renderer(renderer)
{
    CreatePermanentResources();
}

CubeRenderer::~CubeRenderer()
{

}

void CubeRenderer::Update(f32 deltaTime)
{

}

void CubeRenderer::AddGeometryPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex)
{
	struct Data
	{
        Renderer::ImageMutableResource color;
        Renderer::DepthImageMutableResource depth;

        Renderer::DescriptorSetResource globalSet;
        Renderer::DescriptorSetResource terrainSet;
	};
    renderGraph->AddPass<Data>("TerrainGeometry",
        [=, &resources](Data& data, Renderer::RenderGraphBuilder& builder) // Setup
        {
            data.color = builder.Write(resources.finalColor, Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::LOAD);
            data.depth = builder.Write(resources.depth, Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::LOAD);

            data.globalSet = builder.Use(resources.globalDescriptorSet);
            data.terrainSet = builder.Use(_geometryDescriptorSet);

            return true; // Return true from setup to enable this pass, return false to disable it
        },
        [=](Data& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList) // Execute
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
            vec2 renderTargetSize = graphResources.GetImageDimensions(data.color);

            commandList.SetViewport(0, 0, renderTargetSize.x, renderTargetSize.y, 0.0f, 1.0f);
            commandList.SetScissorRect(0, static_cast<u32>(renderTargetSize.x), 0, static_cast<u32>(renderTargetSize.y));

            commandList.BeginPipeline(pipeline);

            commandList.SetIndexBuffer(_indices.GetBuffer(), Renderer::IndexFormat::UInt16);
            commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::GLOBAL, data.globalSet, frameIndex);
            commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::PER_DRAW, data.terrainSet, frameIndex);

            commandList.DrawIndexed(36, 1, 0, 0, 0);
            commandList.EndPipeline(pipeline);
        });
}

void CubeRenderer::CreatePermanentResources()
{
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

    // Index buffer
    _vertices.SetDebugName("TerrainVertices");
    _vertices.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);

    {
        std::vector<Vertex>& vertices = _vertices.Get();

        vertices.push_back({vec4(-1.0f, 1.0f, 1.0f, 1.0f), vec4(1.0f, 0.0f, 0.0f, 1.0f) });
        vertices.push_back({vec4(-1.0f, -1.0f, 1.0f, 1.0f), vec4(0.0f, 1.0f, 0.0f, 1.0f) });
        vertices.push_back({vec4(1.0f, 1.0f, 1.0f, 1.0f), vec4(0.0f, 0.0f, 1.0f, 1.0f) });
        vertices.push_back({vec4(1.0f, -1.0f, 1.0f, 1.0f), vec4(1.0f, 1.0f, 0.0f, 1.0f) });
        vertices.push_back({vec4(-1.0f, 1.0f, -1.0f, 1.0f), vec4(1.0f, 0.0f, 1.0f, 1.0f) });
        vertices.push_back({vec4(-1.0f, -1.0f, -1.0f, 1.0f), vec4(0.0f, 1.0f, 1.0f, 1.0f) });
        vertices.push_back({vec4(1.0f, 1.0f, -1.0f, 1.0f), vec4(1.0f, 1.0f, 1.0f, 1.0f) });
        vertices.push_back({vec4(1.0f, -1.0f, -1.0f, 1.0f), vec4(0.0f, 0.0f, 0.0f, 1.0f) });
    }

    _vertices.SyncToGPU(_renderer);
    _geometryDescriptorSet.Bind("_vertices"_h, _vertices.GetBuffer());

    // Index buffer
    _indices.SetDebugName("TerrainIndices");
    _indices.SetUsage(Renderer::BufferUsage::INDEX_BUFFER);

    {
        std::vector<u16>& indices = _indices.Get();
        
        indices.resize(36);

        indices[0] = 0;
        indices[1] = 2;
        indices[2] = 3;
        indices[3] = 0;
        indices[4] = 3;
        indices[5] = 1;

        indices[6] = 2;
        indices[7] = 6;
        indices[8] = 7;
        indices[9] = 2;
        indices[10] = 7;
        indices[11] = 3;

        indices[12] = 6;
        indices[13] = 4;
        indices[14] = 5;
        indices[15] = 6;
        indices[16] = 5;
        indices[17] = 7;

        indices[18] = 4;
        indices[19] = 0;
        indices[20] = 1;
        indices[21] = 4;
        indices[22] = 1;
        indices[23] = 5;

        indices[24] = 0;
        indices[25] = 4;
        indices[26] = 6;
        indices[27] = 0;
        indices[28] = 6;
        indices[29] = 2;

        indices[30] = 1;
        indices[31] = 7;
        indices[32] = 5;
        indices[33] = 1;
        indices[34] = 3;
        indices[35] = 7;
    }

    _indices.SyncToGPU(_renderer);
}