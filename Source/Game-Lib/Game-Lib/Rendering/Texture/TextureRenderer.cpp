#include "TextureRenderer.h"
#include "Game-Lib/Application/EnttRegistries.h"
#include "Game-Lib/ECS/Components/DisplayInfo.h"
#include "Game-Lib/ECS/Components/Tags.h"
#include "Game-Lib/ECS/Components/UnitCustomization.h"
#include "Game-Lib/ECS/Singletons/CharacterSingleton.h"
#include "Game-Lib/ECS/Singletons/Database/ClientDBSingleton.h"
#include "Game-Lib/ECS/Singletons/Database/ItemSingleton.h"
#include "Game-Lib/ECS/Singletons/Database/TextureSingleton.h"
#include "Game-Lib/ECS/Util/Database/ItemUtil.h"
#include "Game-Lib/Rendering/Debug/DebugRenderer.h"
#include "Game-Lib/Rendering/RenderResources.h"
#include "Game-Lib/Util/ServiceLocator.h"

#define A_CPU
#include "Game-Lib/Rendering/Downsampler/ffx_a.h"
#include "Game-Lib/Rendering/Downsampler/ffx_spd.h"

#include <Renderer/Renderer.h>
#include <Renderer/RenderGraph.h>

#include <entt/entt.hpp>
#include <imgui.h>


using namespace ECS::Components::UI;

void TextureRenderer::Clear()
{

}

TextureRenderer::TextureRenderer(Renderer::Renderer* renderer, GameRenderer* gameRenderer, DebugRenderer* debugRenderer)
    : _renderer(renderer)
    , _gameRenderer(gameRenderer)
    , _debugRenderer(debugRenderer)
    , _mipResolveDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS)
{
    CreatePermanentResources();
}

void TextureRenderer::Update(f32 deltaTime)
{
    ZoneScoped;

    if (ImGui::Begin("TextureRenderer Debug"))
    {
        entt::registry* gameRegistry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& characterSingleton = gameRegistry->ctx().get<ECS::Singletons::CharacterSingleton>();

        if (gameRegistry->valid(characterSingleton.moverEntity))
        {
            if (auto* unitCustomization = gameRegistry->try_get<ECS::Components::UnitCustomization>(characterSingleton.moverEntity))
            {
                if (unitCustomization->skinTextureID != Renderer::TextureID::Invalid())
                {
                    u64 textureHandle = _renderer->GetImguiTextureID(unitCustomization->skinTextureID);
                    ImGui::Image((ImTextureID)textureHandle, ImVec2(512, 512));
                }

                bool isDirtyTexture = false;
                bool isDirtyGeoset = false;

                i32 skinID = unitCustomization->skinID;
                ImGui::Text("Skin ID");
                if (ImGui::InputInt("##Skin ID", &skinID, 1, 1))
                {
                    unitCustomization->skinID = skinID;
                    isDirtyTexture = true;
                }

                i32 faceID = unitCustomization->faceID;
                ImGui::Text("Face ID");
                if (ImGui::InputInt("##Face ID", &faceID, 1, 1))
                {
                    unitCustomization->faceID = faceID;
                    isDirtyTexture = true;
                }

                i32 hairStyleID = unitCustomization->hairStyleID;
                ImGui::Text("Hair Style ID");
                if (ImGui::InputInt("##Hair Style ID", &hairStyleID, 1, 1))
                {
                    unitCustomization->flags.hairChanged = true;
                    unitCustomization->hairStyleID = hairStyleID;
                    isDirtyGeoset = true;
                }

                i32 hairColorID = unitCustomization->hairColorID;
                ImGui::Text("Hair Color ID");
                if (ImGui::InputInt("##Hair Color ID", &hairColorID, 1, 1))
                {
                    unitCustomization->flags.hairChanged = true;
                    unitCustomization->hairColorID = hairColorID;
                }

                i32 piercingsID = unitCustomization->piercingsID;
                ImGui::Text("Piercing ID");
                if (ImGui::InputInt("##Piercing ID", &piercingsID, 1, 1))
                {
                    unitCustomization->piercingsID = piercingsID;
                    isDirtyGeoset = true;
                }

                if (isDirtyTexture || unitCustomization->flags.hairChanged)
                {
                    gameRegistry->emplace_or_replace<ECS::Components::UnitRebuildSkinTexture>(characterSingleton.moverEntity);
                }

                if (isDirtyGeoset)
                {
                    gameRegistry->emplace_or_replace<ECS::Components::UnitRebuildGeosets>(characterSingleton.moverEntity);
                }
            }
        }
    }
    ImGui::End();
}

void TextureRenderer::AddTexturePass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex)
{
    struct Data
    {
        u32 numRenderTextureToTextureRequests;
        robin_hood::unordered_map<Renderer::ImageFormat, Renderer::DescriptorSetResource> descriptorSets;
        Renderer::DescriptorSetResource mipResolveDescriptorSet;
    };
    renderGraph->AddPass<Data>("TextureRenderer",
        [this, &resources](Data& data, Renderer::RenderGraphBuilder& builder) // Setup
        {
            using BufferUsage = Renderer::BufferPassUsage;

            // Init pipelines
            data.numRenderTextureToTextureRequests = static_cast<u32>(_renderTextureToTextureRequests.try_dequeue_bulk(_renderTextureToTextureWork.begin(), 256));
            for(u32 i = 0; i < data.numRenderTextureToTextureRequests; i++)
            {
                RenderTextureToTextureRequest& request = _renderTextureToTextureWork[i];

                // Make sure the pipeline for this format exists
                Renderer::ImageFormat format = _renderer->GetDesc(request.dst).format;
                GetPipelineForFormat(format);

                // Add textures to array
                _renderTextureToTextureWorkTextureArrayIndex[i] = _renderer->AddTextureToArray(request.src, _sourceTextures);
            }
            _renderer->FlushTextureArrayDescriptors(_sourceTextures);

            for(auto& [format, descriptorSet] : _descriptorSets)
            {
                data.descriptorSets[format] = builder.Use(descriptorSet);
            }

            data.mipResolveDescriptorSet = builder.Use(_mipResolveDescriptorSet);

            builder.Write(_mipAtomicBuffer, BufferUsage::COMPUTE);

            return true;// Return true from setup to enable this pass, return false to disable it
        },
        [this, &resources, frameIndex](Data& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList) // Execute
        {
            GPU_SCOPED_PROFILER_ZONE(commandList, TextureRendering);

            if (data.numRenderTextureToTextureRequests > 0)
            {
                ZoneScopedN("Render Texture To Texture Requests");
                for (u32 i = 0; i < data.numRenderTextureToTextureRequests; i++)
                {
                    RenderTextureToTextureRequest& request = _renderTextureToTextureWork[i];
                    u32 srcArrayIndex = _renderTextureToTextureWorkTextureArrayIndex[i];
                    RenderTextureToTexture(graphResources, commandList, frameIndex, data.descriptorSets, request.dst, request.dstRectMin, request.dstRectMax, srcArrayIndex, request.srcRectMin, request.srcRectMax);
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

                // DEBUG: Enable this to keep rendering requests every frame for easier capture
                //_renderTextureToTextureRequests.enqueue_bulk(&_renderTextureToTextureWork[0], data.numRenderTextureToTextureRequests);
            }
        });
}

Renderer::TextureID TextureRenderer::MakeRenderableCopy(Renderer::TextureID texture, u32 width, u32 height)
{
    Renderer::TextureBaseDesc textureBaseDesc = _renderer->GetDesc(texture);

    Renderer::DataTextureDesc desc;
    desc.width = width > 0 ? width : textureBaseDesc.width;
    desc.height = height > 0 ? height : textureBaseDesc.height;
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
    CreatePipelines();
    InitDescriptorSets();

    Renderer::TextureArrayDesc textureArrayDesc;
    textureArrayDesc.size = 4096;
    _sourceTextures = _renderer->CreateTextureArray(textureArrayDesc);

    _renderTextureToTextureWork.resize(256);
    _renderTextureToTextureWorkTextureArrayIndex.resize(256);

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

    // Blit sampler
    Renderer::SamplerDesc blitSamplerDesc;
    blitSamplerDesc.filter = Renderer::SamplerFilter::MIN_MAG_MIP_LINEAR;
    blitSamplerDesc.addressU = Renderer::TextureAddressMode::CLAMP;
    blitSamplerDesc.addressV = Renderer::TextureAddressMode::CLAMP;
    blitSamplerDesc.addressW = Renderer::TextureAddressMode::CLAMP;
    blitSamplerDesc.minLOD = 0.f;
    blitSamplerDesc.maxLOD = 16.f;

    _blitSampler = _renderer->CreateSampler(blitSamplerDesc);

    // Mip resolve sampler
    Renderer::SamplerDesc mipDownSamplerDesc;
    mipDownSamplerDesc.filter = Renderer::SamplerFilter::MIN_MAG_MIP_LINEAR;
    mipDownSamplerDesc.addressU = Renderer::TextureAddressMode::CLAMP;
    mipDownSamplerDesc.addressV = Renderer::TextureAddressMode::CLAMP;
    mipDownSamplerDesc.addressW = Renderer::TextureAddressMode::CLAMP;
    mipDownSamplerDesc.minLOD = 0.f;
    mipDownSamplerDesc.maxLOD = 16.f;

    _mipResolveSampler = _renderer->CreateSampler(mipDownSamplerDesc);
    _mipResolveDescriptorSet.Bind("srcSampler", _mipResolveSampler);
}

void TextureRenderer::CreatePipelines()
{
    // Mip resolve compute pipeline
    Renderer::ComputePipelineDesc pipelineDesc;
    pipelineDesc.debugName = "Downsample Mips";

    Renderer::ComputeShaderDesc shaderDesc;
    shaderDesc.shaderEntry = _gameRenderer->GetShaderEntry("DownSampler/SinglePassDownsampler.cs"_h, "DownSampler/SinglePassDownsampler.cs");
    pipelineDesc.computeShader = _renderer->LoadShader(shaderDesc);

    _mipDownsamplerPipeline = _renderer->CreatePipeline(pipelineDesc);
}

void TextureRenderer::InitDescriptorSets()
{
    _mipResolveDescriptorSet.RegisterPipeline(_renderer, _mipDownsamplerPipeline);
    _mipResolveDescriptorSet.Init(_renderer);
}

void TextureRenderer::RenderTextureToTexture(Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList, u32 frameIndex, robin_hood::unordered_map<Renderer::ImageFormat, Renderer::DescriptorSetResource>& descriptorSets, Renderer::TextureID dst, const vec2& dstRectMin, const vec2& dstRectMax, u32 srcArrayIndex, const vec2& srcRectMin, const vec2& srcRectMax)
{
    Renderer::TextureBaseDesc destinationDesc = _renderer->GetDesc(dst);
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
        static_cast<i32>(pixelOffset.x),
        static_cast<i32>(pixelOffset.x + pixelExtent.x),
        static_cast<i32>(pixelOffset.y),
        static_cast<i32>(pixelOffset.y + pixelExtent.y)
    );

    Renderer::ImageFormat format = _renderer->GetDesc(dst).format;
    Renderer::GraphicsPipelineID pipeline = GetPipelineForFormat(format);
    commandList.BeginPipeline(pipeline);

    Renderer::DescriptorSetResource& descriptorSet = descriptorSets[format];

    //descriptorSet.Bind2("_texture", src);
    commandList.BindDescriptorSet(descriptorSet, frameIndex);

    struct BlitConstant
    {
        vec4 colorMultiplier;
        vec4 additiveColor;
        vec4 uvOffsetAndExtent;
        u32 channelRedirectors;
        u32 textureIndex;
    };

    BlitConstant* constants = graphResources.FrameNew<BlitConstant>();
    constants->colorMultiplier = vec4(1, 1, 1, 1);
    constants->additiveColor = vec4(0, 0, 0, 0);
    constants->uvOffsetAndExtent = vec4(srcRectMin, srcRectMax - srcRectMin);

    constants->channelRedirectors = 0x03020100;
    constants->textureIndex = srcArrayIndex;

    commandList.PushConstant(constants, 0, sizeof(BlitConstant));

    commandList.Draw(3, 1, 0, 0);

    commandList.EndPipeline(pipeline);
    commandList.EndRenderPass(renderPassDesc);

    _texturesNeedingMipResolve.insert(static_cast<Renderer::TextureID::type>(dst));
}

void TextureRenderer::ResolveMips(Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList, u32 frameIndex, Renderer::DescriptorSetResource& descriptorSet, Renderer::TextureID textureID)
{
    Renderer::TextureBaseDesc textureDesc = _renderer->GetDesc(textureID);

    Renderer::TextureRenderPassDesc renderPassDesc;
    renderPassDesc.renderTargets[0] = textureID;

    commandList.BeginTextureComputeWritePass(renderPassDesc);
    commandList.BeginPipeline(_mipDownsamplerPipeline);

    varAU2(dispatchThreadGroupCountXY);
    varAU2(workGroupOffset);
    varAU2(numWorkGroupsAndMips);
    varAU4(rectInfo) = initAU4(0, 0, static_cast<AU1>(textureDesc.width), static_cast<AU1>(textureDesc.height)); // left, top, width, height
    SpdSetup(dispatchThreadGroupCountXY, workGroupOffset, numWorkGroupsAndMips, rectInfo);

    numWorkGroupsAndMips[1] = numWorkGroupsAndMips[1] - 1; // We seem to disagree how many mips we have...

    descriptorSet.Bind("imgSrc", textureID);
    descriptorSet.Bind("imgDst", textureID, 1, numWorkGroupsAndMips[1]);
    descriptorSet.Bind("imgDst5", textureID, 6);

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

    commandList.BindDescriptorSet(descriptorSet, frameIndex);

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

    return CreatePipeline(format);
}

Renderer::GraphicsPipelineID TextureRenderer::CreatePipeline(Renderer::ImageFormat format)
{
    Renderer::GraphicsPipelineDesc pipelineDesc;

    // Shaders
    Renderer::VertexShaderDesc vertexShaderDesc;
    vertexShaderDesc.shaderEntry = _gameRenderer->GetShaderEntry("Blitting/Blit.vs"_h, "Blitting/Blit.vs");;
    pipelineDesc.states.vertexShader = _renderer->LoadShader(vertexShaderDesc);

    Renderer::PixelShaderDesc pixelShaderDesc;
    pixelShaderDesc.shaderEntry = _gameRenderer->GetShaderEntry("Blitting/BlitSampleTexArray.ps"_h, "Blitting/BlitSampleTexArray.ps");
    pipelineDesc.states.pixelShader = _renderer->LoadShader(pixelShaderDesc);

    // Depth state
    pipelineDesc.states.depthStencilState.depthEnable = false;

    // Rasterizer state
    pipelineDesc.states.rasterizerState.cullMode = Renderer::CullMode::NONE;
    pipelineDesc.states.rasterizerState.frontFaceMode = Renderer::FrontFaceState::COUNTERCLOCKWISE;

    // Render targets
    pipelineDesc.states.renderTargetFormats[0] = format;

    // Blending
    pipelineDesc.states.blendState.renderTargets[0].blendEnable = true;
    pipelineDesc.states.blendState.renderTargets[0].srcBlend = Renderer::BlendMode::SRC_ALPHA;
    pipelineDesc.states.blendState.renderTargets[0].destBlend = Renderer::BlendMode::INV_SRC_ALPHA;
    pipelineDesc.states.blendState.renderTargets[0].srcBlendAlpha = Renderer::BlendMode::ONE;
    pipelineDesc.states.blendState.renderTargets[0].destBlendAlpha = Renderer::BlendMode::ONE;
    pipelineDesc.states.blendState.renderTargets[0].blendOpAlpha = Renderer::BlendOp::MAX;

    Renderer::GraphicsPipelineID pipeline = _renderer->CreatePipeline(pipelineDesc);
    _pipelines[format] = pipeline;

    // Create descriptor set for this pipeline
    auto [it, inserted] = _descriptorSets.try_emplace(format, Renderer::DescriptorSet(Renderer::DescriptorSetSlot::PER_PASS));

    Renderer::DescriptorSet& descriptorSet = it->second;
    descriptorSet.RegisterPipeline(_renderer, pipeline);
    descriptorSet.Init(_renderer);

    descriptorSet.Bind("_sampler"_h, _blitSampler);
    descriptorSet.Bind("_textures"_h, _sourceTextures);

    return pipeline;
}
