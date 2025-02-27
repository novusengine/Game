#include "TextureRenderer.h"
#include "Game-Lib/Rendering/Debug/DebugRenderer.h"
#include "Game-Lib/Rendering/RenderResources.h"
#include "Game-Lib/Util/ServiceLocator.h"
#define A_CPU
#include "Game-Lib/Rendering/Downsampler/ffx_a.h"
#include "Game-Lib/Rendering/Downsampler/ffx_spd.h"

#include <Renderer/Renderer.h>
#include <Renderer/RenderGraph.h>

#include <imgui.h>


using namespace ECS::Components::UI;

void TextureRenderer::Clear()
{

}

TextureRenderer::TextureRenderer(Renderer::Renderer* renderer, DebugRenderer* debugRenderer)
    : _renderer(renderer)
    , _debugRenderer(debugRenderer)
{
    CreatePermanentResources();
}

void TextureRenderer::Update(f32 deltaTime)
{
    ZoneScoped;

    if (ImGui::Begin("TextureRenderer Debug"))
    {
        if (_debugTexture != Renderer::TextureID::Invalid())
        {
            u64 textureHandle = _renderer->GetImguiTextureID(_debugTexture);
            ImGui::Image((ImTextureID)textureHandle, ImVec2(256, 256));
        }
    }
    ImGui::End();
}

void TextureRenderer::AddTexturePass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex)
{
    struct Data
    {
        Renderer::DescriptorSetResource descriptorSet;
        Renderer::DescriptorSetResource mipResolveDescriptorSet;
    };
    renderGraph->AddPass<Data>("TextureRenderer",
        [this, &resources](Data& data, Renderer::RenderGraphBuilder& builder) // Setup
        {
            using BufferUsage = Renderer::BufferPassUsage;

            data.descriptorSet = builder.Use(_descriptorSet);
            data.mipResolveDescriptorSet = builder.Use(_mipResolveDescriptorSet);

            builder.Write(_mipAtomicBuffer, BufferUsage::COMPUTE);

            return true;// Return true from setup to enable this pass, return false to disable it
        },
        [this, &resources, frameIndex](Data& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList) // Execute
        {
            GPU_SCOPED_PROFILER_ZONE(commandList, TextureRendering);

            u32 numRenderTextureToTextureRequests = static_cast<u32>(_renderTextureToTextureRequests.try_dequeue_bulk(_renderTextureToTextureWork.begin(), 256));
            if (numRenderTextureToTextureRequests > 0)
            {
                ZoneScopedN("Render Texture To Texture Requests");
                for (u32 i = 0; i < numRenderTextureToTextureRequests; i++)
                {
                    RenderTextureToTextureRequest& request = _renderTextureToTextureWork[i];
                    RenderTextureToTexture(graphResources, commandList, frameIndex, data.descriptorSet, request.dst, request.dstRectMin, request.dstRectMax, request.src, request.srcRectMin, request.srcRectMax);
                }

                // Resolve mips
                for (u32 texture : _texturesNeedingMipResolve)
                {
                    Renderer::TextureID textureID = Renderer::TextureID(texture);
                    ResolveMips(graphResources, commandList, frameIndex, data.mipResolveDescriptorSet, textureID);
                }

                // Reset the viewport and scissor
                vec2 renderSize = _renderer->GetRenderSize();
                commandList.SetViewport(0, 0, renderSize.x, renderSize.y, 0.0f, 1.0f);
                commandList.SetScissorRect(0, static_cast<u32>(renderSize.x), 0, static_cast<u32>(renderSize.y));

                if (_debugEveryFrame)
                {
                    _renderTextureToTextureRequests.enqueue_bulk(_renderTextureToTextureWork.begin(), numRenderTextureToTextureRequests);
                }
            }
        });
}

Renderer::TextureID TextureRenderer::MakeRenderableCopy(Renderer::TextureID texture)
{
    Renderer::TextureBaseDesc textureBaseDesc = _renderer->GetTextureDesc(texture);

    Renderer::DataTextureDesc desc;
    desc.width = textureBaseDesc.width;
    desc.height = textureBaseDesc.height;
    desc.layers = textureBaseDesc.layers;
    desc.mipLevels = textureBaseDesc.mipLevels;

    desc.format = Renderer::ToRenderable(textureBaseDesc.format);
    desc.debugName = textureBaseDesc.debugName + " (W)";
    desc.renderable = true;

    Renderer::TextureID newTexture = _renderer->CreateDataTexture(desc);
    RequestRenderTextureToTexture(newTexture, { 0, 0 }, { 1, 1 }, texture, { 0, 0 }, { 1, 1 });

    return newTexture;
}

void TextureRenderer::RequestRenderTextureToTexture(Renderer::TextureID dst, const vec2& dstRectMin, const vec2& dstRectMax, Renderer::TextureID src, const vec2& srcRectMin, const vec2& srcRectMax)
{
    RenderTextureToTextureRequest renderTextureToTextureRequest =
    {
        .dst = dst,
        .dstRectMin = dstRectMin,
        .dstRectMax = dstRectMax,
        .src = src,
        .srcRectMin = srcRectMin,
        .srcRectMax = srcRectMax,
    };
    _renderTextureToTextureRequests.enqueue(renderTextureToTextureRequest);
}

void TextureRenderer::CreatePermanentResources()
{
    _renderTextureToTextureWork.resize(256);

    // Mip resolve atomic buffer
    Renderer::BufferDesc mipAtomicBufferDesc;
    mipAtomicBufferDesc.name = "TextureMipResolveAtomicBuffer";
    mipAtomicBufferDesc.size = sizeof(u32) * 6;
    mipAtomicBufferDesc.usage = Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION;

    _mipAtomicBuffer = _renderer->CreateAndFillBuffer(_mipAtomicBuffer, mipAtomicBufferDesc, [](void* mappedMemory, size_t size)
    {
        memset(mappedMemory, 0, size);
    });
    _mipResolveDescriptorSet.Bind("spdGlobalAtomic", _mipAtomicBuffer);

    // Mip resolve sampler
    Renderer::SamplerDesc mipDownSamplerDesc;
    mipDownSamplerDesc.filter = Renderer::SamplerFilter::MINIMUM_MIN_MAG_MIP_LINEAR;
    mipDownSamplerDesc.addressU = Renderer::TextureAddressMode::CLAMP;
    mipDownSamplerDesc.addressV = Renderer::TextureAddressMode::CLAMP;
    mipDownSamplerDesc.addressW = Renderer::TextureAddressMode::CLAMP;
    mipDownSamplerDesc.minLOD = 0.f;
    mipDownSamplerDesc.maxLOD = 16.f;

    _mipResolveSampler = _renderer->CreateSampler(mipDownSamplerDesc);
    _mipResolveDescriptorSet.Bind("srcSampler", _mipResolveSampler);

    // Mip resolve compute pipeline
    Renderer::ComputePipelineDesc pipelineDesc;
    pipelineDesc.debugName = "Downsample Mips";

    Renderer::ComputeShaderDesc shaderDesc;
    shaderDesc.path = "DownSampler/SinglePassDownsampler.cs.hlsl";
    pipelineDesc.computeShader = _renderer->LoadShader(shaderDesc);

    _mipDownsamplerPipeline = _renderer->CreatePipeline(pipelineDesc);

    // DEBUG
    // Load a texture
    Renderer::TextureDesc textureDesc;
    textureDesc.path = "Data/Texture/character/human/female/humanfemaleskin00_04.dds";

    Renderer::TextureID baseTexture = _renderer->LoadTexture(textureDesc);

    _debugTexture = MakeRenderableCopy(baseTexture);

    // Load a texture to layer on top
    Renderer::TextureDesc overlayDesc;
    overlayDesc.path = "Data/Texture/character/human/female/humanfemalenakedtorsoskin00_09.dds";
    Renderer::TextureID overlayTexture = _renderer->LoadTexture(overlayDesc);

    RequestRenderTextureToTexture(_debugTexture, vec2(0.25f, 0.25f), vec2(0.75f, 0.75f), overlayTexture, vec2(0.25f, 0.25f), vec2(0.75f, 0.75f));
}

void TextureRenderer::RenderTextureToTexture(Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList, u32 frameIndex, Renderer::DescriptorSetResource& descriptorSet, Renderer::TextureID dst, const vec2& dstRectMin, const vec2& dstRectMax, Renderer::TextureID src, const vec2& srcRectMin, const vec2& srcRectMax)
{
    Renderer::TextureBaseDesc destinationDesc = _renderer->GetTextureDesc(dst);
    vec2 dstSize = vec2(destinationDesc.width, destinationDesc.height);

    Renderer::TextureRenderPassDesc renderPassDesc;
    renderPassDesc.renderTargets[0] = dst;
    renderPassDesc.offset = dstRectMin * dstSize;
    renderPassDesc.extent = (dstRectMax - dstRectMin) * dstSize;

    commandList.BeginRenderPass(renderPassDesc);

    // Compute the pixel coordinates from the normalized rectangle
    vec2 pixelOffset = dstRectMin * dstSize;
    vec2 pixelExtent = (dstRectMax - dstRectMin) * dstSize;

    // Set the viewport using pixel-based values: x, y, width, height, minDepth, maxDepth
    commandList.SetViewport(pixelOffset.x, pixelOffset.y, pixelExtent.x, pixelExtent.y, 0.0f, 1.0f);

    // Set the scissor rectangle using pixel coordinates: left, right, top, bottom
    commandList.SetScissorRect(
        static_cast<int>(pixelOffset.x),
        static_cast<int>(pixelOffset.x + pixelExtent.x),
        static_cast<int>(pixelOffset.y),
        static_cast<int>(pixelOffset.y + pixelExtent.y)
    );

    Renderer::ImageFormat format = _renderer->GetTextureDesc(dst).format;
    Renderer::GraphicsPipelineID pipeline = GetPipelineForFormat(format);
    commandList.BeginPipeline(pipeline);

    descriptorSet.BindRead("_texture", src);
    commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::GLOBAL, descriptorSet, frameIndex);

    struct BlitConstant
    {
        vec4 colorMultiplier;
        vec4 additiveColor;
        vec4 uvOffsetAndExtent;
        u32 channelRedirectors;
    };

    BlitConstant* constants = graphResources.FrameNew<BlitConstant>();
    constants->colorMultiplier = vec4(1, 1, 1, 1);
    constants->additiveColor = vec4(0, 0, 0, 0);
    constants->uvOffsetAndExtent = vec4(srcRectMin, srcRectMax - srcRectMin);

    constants->channelRedirectors = 0x03020100;

    commandList.PushConstant(constants, 0, sizeof(BlitConstant));

    commandList.Draw(3, 1, 0, 0);

    commandList.EndPipeline(pipeline);
    commandList.EndRenderPass(renderPassDesc);

    _texturesNeedingMipResolve.insert(static_cast<Renderer::TextureID::type>(dst));
}

void TextureRenderer::ResolveMips(Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList, u32 frameIndex, Renderer::DescriptorSetResource& descriptorSet, Renderer::TextureID textureID)
{
    Renderer::TextureBaseDesc textureDesc = _renderer->GetTextureDesc(textureID);

    Renderer::TextureRenderPassDesc renderPassDesc;
    renderPassDesc.renderTargets[0] = textureID;

    commandList.BeginTextureComputeWritePass(renderPassDesc);
    commandList.BeginPipeline(_mipDownsamplerPipeline);

    descriptorSet.BindRead("imgSrc", textureID);// , 0);
    descriptorSet.BindWrite("imgDst", textureID, 1, 12);
    descriptorSet.BindWrite("imgDst5", textureID, 6);

    varAU2(dispatchThreadGroupCountXY);
    varAU2(workGroupOffset);
    varAU2(numWorkGroupsAndMips);
    varAU4(rectInfo) = initAU4(0, 0, static_cast<AU1>(textureDesc.width), static_cast<AU1>(textureDesc.height)); // left, top, width, height
    SpdSetup(dispatchThreadGroupCountXY, workGroupOffset, numWorkGroupsAndMips, rectInfo);

    struct Constants
    {
        u32 mips;
        u32 numWorkGroups;
        uvec2 workGroupOffset;
        vec2 invInputSize;
    };

    Constants* constants = graphResources.FrameNew<Constants>();
    constants->numWorkGroups = numWorkGroupsAndMips[0];
    constants->mips = numWorkGroupsAndMips[1];
    constants->workGroupOffset[0] = workGroupOffset[0];
    constants->workGroupOffset[1] = workGroupOffset[1];
    constants->invInputSize[0] = 1.0f / static_cast<f32>(textureDesc.width);
    constants->invInputSize[1] = 1.0f / static_cast<f32>(textureDesc.height);

    commandList.PushConstant(constants, 0, sizeof(Constants));

    commandList.BindDescriptorSet(Renderer::PER_PASS, descriptorSet, frameIndex);

    commandList.Dispatch(dispatchThreadGroupCountXY[0], dispatchThreadGroupCountXY[1], 1);

    commandList.EndPipeline(_mipDownsamplerPipeline);
    commandList.EndTextureComputeWritePass(renderPassDesc);
}

Renderer::GraphicsPipelineID TextureRenderer::GetPipelineForFormat(Renderer::ImageFormat format)
{
    if (_pipelines.contains(format))
    {
        return _pipelines[format];
    }

    Renderer::GraphicsPipelineID pipeline = CreatePipeline(format);
    _pipelines[format] = pipeline;

    return pipeline;
}

Renderer::GraphicsPipelineID TextureRenderer::CreatePipeline(Renderer::ImageFormat format)
{
    Renderer::GraphicsPipelineDesc pipelineDesc;

    // Shaders
    Renderer::VertexShaderDesc vertexShaderDesc;
    vertexShaderDesc.path = "Blitting/blit.vs.hlsl";

    pipelineDesc.states.vertexShader = _renderer->LoadShader(vertexShaderDesc);

    Renderer::PixelShaderDesc pixelShaderDesc;
    pixelShaderDesc.path = "Blitting/blit.ps.hlsl";

    std::string textureTypeName = GetTextureTypeName(format);
    pixelShaderDesc.AddPermutationField("TEX_TYPE", textureTypeName);
    pipelineDesc.states.pixelShader = _renderer->LoadShader(pixelShaderDesc);

    // Depth state
    pipelineDesc.states.depthStencilState.depthEnable = false;

    // Rasterizer state
    pipelineDesc.states.rasterizerState.cullMode = Renderer::CullMode::NONE;
    pipelineDesc.states.rasterizerState.frontFaceMode = Renderer::FrontFaceState::COUNTERCLOCKWISE;

    // Render targets
    pipelineDesc.states.renderTargetFormats[0] = format;

    // Set pipeline
    return _renderer->CreatePipeline(pipelineDesc); // This will compile the pipeline and return the ID, or just return ID of cached pipeline
}
