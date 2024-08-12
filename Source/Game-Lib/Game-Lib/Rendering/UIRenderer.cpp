#include "UIRenderer.h"
#include "RenderResources.h"

#include <Renderer/Renderer.h>
#include <Renderer/RenderGraph.h>
#include <Renderer/Font.h>
#include <Renderer/Buffer.h>
#include <Renderer/Window.h>
#include <Renderer/Descriptors/FontDesc.h>
#include <Renderer/Descriptors/TextureDesc.h>
#include <Renderer/Descriptors/SamplerDesc.h>

#include <tracy/Tracy.hpp>
//#include <tracy/TracyVulkan.hpp>

UIRenderer::UIRenderer(Renderer::Renderer* renderer) : _renderer(renderer)
{
    CreatePermanentResources();
}

void UIRenderer::Update(f32 deltaTime)
{
}

void UIRenderer::AddImguiPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex, Renderer::ImageID imguiTarget)
{
    // UI Pass
    struct UIPassData
    {
        Renderer::ImageMutableResource color;
    };

    renderGraph->AddPass<UIPassData>("ImguiPass",
        [this, imguiTarget](UIPassData& data, Renderer::RenderGraphBuilder& builder) // Setup
        {
            data.color = builder.Write(imguiTarget, Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::LOAD);

            return true; // Return true from setup to enable this pass, return false to disable it
        },
        [this](UIPassData& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList) // Execute
        {
            GPU_SCOPED_PROFILER_ZONE(commandList, ImguiPass);

            commandList.ImageBarrier(data.color);

            Renderer::GraphicsPipelineDesc pipelineDesc;
            graphResources.InitializePipelineDesc(pipelineDesc);

            // Rasterizer state
            pipelineDesc.states.rasterizerState.cullMode = Renderer::CullMode::BACK;
            //pipelineDesc.states.rasterizerState.frontFaceMode = Renderer::FrontFaceState::COUNTERCLOCKWISE;

            // Render targets
            pipelineDesc.renderTargets[0] = data.color;

            // Blending
            pipelineDesc.states.blendState.renderTargets[0].blendEnable = true;
            pipelineDesc.states.blendState.renderTargets[0].srcBlend = Renderer::BlendMode::SRC_ALPHA;
            pipelineDesc.states.blendState.renderTargets[0].destBlend = Renderer::BlendMode::INV_SRC_ALPHA;
            pipelineDesc.states.blendState.renderTargets[0].srcBlendAlpha = Renderer::BlendMode::ZERO;
            pipelineDesc.states.blendState.renderTargets[0].destBlendAlpha = Renderer::BlendMode::ONE;

            // Panel Shaders
            Renderer::VertexShaderDesc vertexShaderDesc;
            vertexShaderDesc.path = "UI/panel.vs.hlsl";
            pipelineDesc.states.vertexShader = _renderer->LoadShader(vertexShaderDesc);

            Renderer::PixelShaderDesc pixelShaderDesc;
            pixelShaderDesc.path = "UI/panel.ps.hlsl";
            pipelineDesc.states.pixelShader = _renderer->LoadShader(pixelShaderDesc);

            Renderer::GraphicsPipelineID activePipeline = _renderer->CreatePipeline(pipelineDesc);

            // Set viewport
            vec2 renderTargetSize = graphResources.GetImageDimensions(data.color);

            //vec2 windowSize = _renderer->GetWindowSize();
            commandList.SetViewport(0, 0, renderTargetSize.x, renderTargetSize.y, 0.0f, 1.0f);
            commandList.SetScissorRect(0, static_cast<u32>(renderTargetSize.x), 0, static_cast<u32>(renderTargetSize.y));

            commandList.BeginPipeline(activePipeline);
            commandList.DrawImgui();
            commandList.EndPipeline(activePipeline);
        });
}

void UIRenderer::CreatePermanentResources()
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
    _passDescriptorSet.Bind("_sampler"_h, _linearSampler);

    // Index buffer
    static const u32 indexBufferSize = sizeof(u16) * 6;

    Renderer::BufferDesc bufferDesc;
    bufferDesc.name = "IndexBuffer";
    bufferDesc.size = indexBufferSize;
    bufferDesc.usage = Renderer::BufferUsage::INDEX_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION;
    bufferDesc.cpuAccess = Renderer::BufferCPUAccess::AccessNone;

    _indexBuffer = _renderer->CreateBuffer(bufferDesc);

    Renderer::BufferDesc stagingBufferDesc;
    stagingBufferDesc.name = "StagingBuffer";
    stagingBufferDesc.size = indexBufferSize;
    stagingBufferDesc.usage = Renderer::BufferUsage::INDEX_BUFFER | Renderer::BufferUsage::TRANSFER_SOURCE;
    stagingBufferDesc.cpuAccess = Renderer::BufferCPUAccess::WriteOnly;

    Renderer::BufferID stagingBuffer = _renderer->CreateBuffer(stagingBufferDesc);

    u16* index = static_cast<u16*>(_renderer->MapBuffer(stagingBuffer));
    index[0] = 0;
    index[1] = 1;
    index[2] = 2;
    index[3] = 1;
    index[4] = 3;
    index[5] = 2;
    _renderer->UnmapBuffer(stagingBuffer);

    _renderer->QueueDestroyBuffer(stagingBuffer);
    _renderer->CopyBuffer(_indexBuffer, 0, stagingBuffer, 0, indexBufferSize);
}

