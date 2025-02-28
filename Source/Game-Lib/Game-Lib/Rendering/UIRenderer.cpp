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
    ZoneScoped;
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

            Renderer::RenderPassDesc renderPassDesc;
            graphResources.InitializeRenderPassDesc(renderPassDesc);

            // Render targets
            renderPassDesc.renderTargets[0] = data.color;
            commandList.BeginRenderPass(renderPassDesc);

            // Set viewport
            vec2 renderTargetSize = graphResources.GetImageDimensions(data.color);

            commandList.SetViewport(0, 0, renderTargetSize.x, renderTargetSize.y, 0.0f, 1.0f);
            commandList.SetScissorRect(0, static_cast<u32>(renderTargetSize.x), 0, static_cast<u32>(renderTargetSize.y));

            commandList.DrawImgui();
            commandList.EndRenderPass(renderPassDesc);
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

