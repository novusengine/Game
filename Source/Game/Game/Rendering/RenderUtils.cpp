#include "RenderUtils.h"

#include <Renderer/Renderer.h>
#include <Renderer/RenderSettings.h>
#include <Renderer/RenderGraphResources.h>
#include <Renderer/CommandList.h>

#include <Renderer/Descriptors/VertexShaderDesc.h>
#include <Renderer/Descriptors/PixelShaderDesc.h>

Renderer::DescriptorSet RenderUtils::_overlayDescriptorSet;
Renderer::DescriptorSet RenderUtils::_copyDepthToColorRTDescriptorSet;

std::string GetTexTypeName(Renderer::ImageDesc imageDesc)
{
    Renderer::ImageComponentType componentType = Renderer::ToImageComponentType(imageDesc.format);
    std::string componentTypeName = "";

    switch (componentType)
    {
    case Renderer::ImageComponentType::FLOAT:
    case Renderer::ImageComponentType::SNORM:
    case Renderer::ImageComponentType::UNORM:
        componentTypeName = "float";
        break;
    case Renderer::ImageComponentType::SINT:
        componentTypeName = "int";
        break;
    case Renderer::ImageComponentType::UINT:
        componentTypeName = "uint";
        break;
    }

    u8 componentCount = Renderer::ToImageComponentCount(imageDesc.format);
    if (componentCount > 1)
    {
        componentTypeName += std::to_string(componentCount);
    }

    return componentTypeName;
}

std::string GetTexTypeName(Renderer::DepthImageDesc imageDesc)
{
    Renderer::ImageComponentType componentType = Renderer::ToImageComponentType(imageDesc.format);
    std::string componentTypeName = "";

    switch (componentType)
    {
    case Renderer::ImageComponentType::FLOAT:
    case Renderer::ImageComponentType::SNORM:
    case Renderer::ImageComponentType::UNORM:
        componentTypeName = "float";
        break;
    case Renderer::ImageComponentType::SINT:
        componentTypeName = "int";
        break;
    case Renderer::ImageComponentType::UINT:
        componentTypeName = "uint";
        break;
    }

    u8 componentCount = Renderer::ToImageComponentCount(imageDesc.format);
    if (componentCount > 1)
    {
        componentTypeName += std::to_string(componentCount);
    }

    return componentTypeName;
}

void RenderUtils::Blit(Renderer::Renderer* renderer, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList, u32 frameIndex, const BlitParams& params)
{
    commandList.PushMarker("Blit", Color::White);
    commandList.ImageBarrier(params.input);

    Renderer::ImageDesc imageDesc = renderer->GetImageDesc(params.input);

    // Setup pipeline
    Renderer::VertexShaderDesc vertexShaderDesc;
    vertexShaderDesc.path = "Blitting/blit.vs.hlsl";



    Renderer::PixelShaderDesc pixelShaderDesc;
    pixelShaderDesc.path = "Blitting/blit.ps.hlsl";
    std::string texTypeName = GetTexTypeName(imageDesc);
    pixelShaderDesc.AddPermutationField("TEX_TYPE", texTypeName);

    Renderer::GraphicsPipelineDesc pipelineDesc;
    graphResources.InitializePipelineDesc(pipelineDesc);

    pipelineDesc.states.vertexShader = renderer->LoadShader(vertexShaderDesc);
    pipelineDesc.states.pixelShader = renderer->LoadShader(pixelShaderDesc);

    pipelineDesc.renderTargets[0] = params.output;

    pipelineDesc.states.rasterizerState.cullMode = Renderer::CullMode::BACK;
    pipelineDesc.states.rasterizerState.frontFaceMode = Renderer::FrontFaceState::COUNTERCLOCKWISE;

    Renderer::GraphicsPipelineID pipeline = renderer->CreatePipeline(pipelineDesc);
    commandList.BeginPipeline(pipeline);

    u32 mipLevel = params.inputMipLevel;
    if (mipLevel >= imageDesc.mipLevels)
    {
        mipLevel = imageDesc.mipLevels - 1;
    }

    _overlayDescriptorSet.Bind("_sampler", params.sampler);
    _overlayDescriptorSet.Bind("_texture", params.input, mipLevel);
    commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::GLOBAL, &_overlayDescriptorSet, frameIndex);

    struct BlitConstant
    {
        vec4 colorMultiplier;
        vec4 additiveColor;
        u32 channelRedirectors;
    };

    BlitConstant* constants = graphResources.FrameNew<BlitConstant>();
    constants->colorMultiplier = params.colorMultiplier;
    constants->additiveColor = params.additiveColor;

    u32 channelRedirectors = params.channelRedirectors.r;
    channelRedirectors |= (params.channelRedirectors.g << 8);
    channelRedirectors |= (params.channelRedirectors.b << 16);
    channelRedirectors |= (params.channelRedirectors.a << 24);

    constants->channelRedirectors = channelRedirectors;

    commandList.PushConstant(constants, 0, sizeof(BlitConstant));

    commandList.Draw(3, 1, 0, 0);

    commandList.EndPipeline(pipeline);
    commandList.ImageBarrier(params.input);
    commandList.PopMarker();
}

void RenderUtils::DepthBlit(Renderer::Renderer* renderer, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList, u32 frameIndex, const DepthBlitParams& params)
{
    commandList.PushMarker("Blit", Color::White);
    commandList.ImageBarrier(params.input);

    Renderer::DepthImageDesc imageDesc = renderer->GetDepthImageDesc(params.input);

    // Setup pipeline
    Renderer::VertexShaderDesc vertexShaderDesc;
    vertexShaderDesc.path = "Blitting/blit.vs.hlsl";

    Renderer::PixelShaderDesc pixelShaderDesc;
    pixelShaderDesc.path = "Blitting/blit.ps.hlsl";
    std::string texTypeName = GetTexTypeName(imageDesc);
    pixelShaderDesc.AddPermutationField("TEX_TYPE", texTypeName);

    Renderer::GraphicsPipelineDesc pipelineDesc;
    graphResources.InitializePipelineDesc(pipelineDesc);

    pipelineDesc.states.vertexShader = renderer->LoadShader(vertexShaderDesc);
    pipelineDesc.states.pixelShader = renderer->LoadShader(pixelShaderDesc);

    pipelineDesc.renderTargets[0] = params.output;

    pipelineDesc.states.rasterizerState.cullMode = Renderer::CullMode::BACK;
    pipelineDesc.states.rasterizerState.frontFaceMode = Renderer::FrontFaceState::COUNTERCLOCKWISE;

    Renderer::GraphicsPipelineID pipeline = renderer->CreatePipeline(pipelineDesc);
    commandList.BeginPipeline(pipeline);

    _overlayDescriptorSet.Bind("_sampler", params.sampler);
    _overlayDescriptorSet.Bind("_texture", params.input);
    commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::GLOBAL, &_overlayDescriptorSet, frameIndex);

    struct BlitConstant
    {
        vec4 colorMultiplier;
        vec4 additiveColor;
        u32 channelRedirectors;
    };

    BlitConstant* constants = graphResources.FrameNew<BlitConstant>();
    constants->colorMultiplier = params.colorMultiplier;
    constants->additiveColor = params.additiveColor;

    u32 channelRedirectors = params.channelRedirectors.r;
    channelRedirectors |= (params.channelRedirectors.g << 8);
    channelRedirectors |= (params.channelRedirectors.b << 16);
    channelRedirectors |= (params.channelRedirectors.a << 24);

    constants->channelRedirectors = channelRedirectors;

    commandList.PushConstant(constants, 0, sizeof(BlitConstant));

    commandList.Draw(3, 1, 0, 0);

    commandList.EndPipeline(pipeline);
    commandList.ImageBarrier(params.input);
    commandList.PopMarker();
}

void RenderUtils::Overlay(Renderer::Renderer* renderer, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList, u32 frameIndex, const OverlayParams& params)
{
    commandList.PushMarker("Overlay", Color::White);
    commandList.ImageBarrier(params.overlayImage);

    Renderer::ImageDesc imageDesc = renderer->GetImageDesc(params.overlayImage);

    // Setup pipeline
    Renderer::VertexShaderDesc vertexShaderDesc;
    vertexShaderDesc.path = "Blitting/blit.vs.hlsl";

    Renderer::PixelShaderDesc pixelShaderDesc;
    pixelShaderDesc.path = "Blitting/blit.ps.hlsl";
    std::string texTypeName = GetTexTypeName(imageDesc);
    pixelShaderDesc.AddPermutationField("TEX_TYPE", texTypeName);

    Renderer::GraphicsPipelineDesc pipelineDesc;
    graphResources.InitializePipelineDesc(pipelineDesc);

    pipelineDesc.states.vertexShader = renderer->LoadShader(vertexShaderDesc);
    pipelineDesc.states.pixelShader = renderer->LoadShader(pixelShaderDesc);

    pipelineDesc.renderTargets[0] = params.baseImage;

    pipelineDesc.states.rasterizerState.cullMode = Renderer::CullMode::BACK;
    pipelineDesc.states.rasterizerState.frontFaceMode = Renderer::FrontFaceState::COUNTERCLOCKWISE;

    pipelineDesc.states.blendState.renderTargets[0].blendEnable = true;
    pipelineDesc.states.blendState.renderTargets[0].blendOp = Renderer::BlendOp::ADD;
    pipelineDesc.states.blendState.renderTargets[0].srcBlend = Renderer::BlendMode::SRC_ALPHA;
    pipelineDesc.states.blendState.renderTargets[0].destBlend = Renderer::BlendMode::ONE;

    Renderer::GraphicsPipelineID pipeline = renderer->CreatePipeline(pipelineDesc);

    commandList.BeginPipeline(pipeline);

    u32 mipLevel = params.mipLevel;
    if (mipLevel >= imageDesc.mipLevels)
    {
        mipLevel = imageDesc.mipLevels - 1;
    }

    _overlayDescriptorSet.Bind("_sampler", params.sampler);
    _overlayDescriptorSet.Bind("_texture", params.overlayImage, mipLevel);
    commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::GLOBAL, &_overlayDescriptorSet, frameIndex);

    struct BlitConstant
    {
        vec4 colorMultiplier;
        vec4 additiveColor;
        u32 channelRedirectors;
    };

    BlitConstant* constants = graphResources.FrameNew<BlitConstant>();
    constants->colorMultiplier = params.colorMultiplier;
    constants->additiveColor = params.additiveColor;

    u32 channelRedirectors = params.channelRedirectors.r;
    channelRedirectors |= (params.channelRedirectors.g << 8);
    channelRedirectors |= (params.channelRedirectors.b << 16);
    channelRedirectors |= (params.channelRedirectors.a << 24);

    constants->channelRedirectors = channelRedirectors;

    commandList.PushConstant(constants, 0, sizeof(BlitConstant));

    commandList.Draw(3, 1, 0, 0);

    commandList.EndPipeline(pipeline);
    commandList.ImageBarrier(params.overlayImage);
    commandList.PopMarker();
}

void RenderUtils::DepthOverlay(Renderer::Renderer* renderer, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList, u32 frameIndex, const DepthOverlayParams& params)
{
    commandList.PushMarker("DepthOverlay", Color::White);
    commandList.ImageBarrier(params.overlayImage);

    // Setup pipeline
    Renderer::VertexShaderDesc vertexShaderDesc;
    vertexShaderDesc.path = "Blitting/blit.vs.hlsl";

    Renderer::DepthImageDesc imageDesc = renderer->GetDepthImageDesc(params.overlayImage);

    Renderer::ImageComponentType componentType = Renderer::ToImageComponentType(imageDesc.format);
    std::string componentTypeName = "";

    Renderer::PixelShaderDesc pixelShaderDesc;
    pixelShaderDesc.path = "Blitting/blit.ps.hlsl";
    std::string texTypeName = GetTexTypeName(imageDesc);
    pixelShaderDesc.AddPermutationField("TEX_TYPE", texTypeName);

    Renderer::GraphicsPipelineDesc pipelineDesc;
    graphResources.InitializePipelineDesc(pipelineDesc);

    pipelineDesc.states.vertexShader = renderer->LoadShader(vertexShaderDesc);
    pipelineDesc.states.pixelShader = renderer->LoadShader(pixelShaderDesc);

    pipelineDesc.renderTargets[0] = params.baseImage;

    pipelineDesc.states.rasterizerState.cullMode = Renderer::CullMode::BACK;
    pipelineDesc.states.rasterizerState.frontFaceMode = Renderer::FrontFaceState::COUNTERCLOCKWISE;

    pipelineDesc.states.blendState.renderTargets[0].blendEnable = true;
    pipelineDesc.states.blendState.renderTargets[0].blendOp = Renderer::BlendOp::ADD;
    pipelineDesc.states.blendState.renderTargets[0].srcBlend = Renderer::BlendMode::SRC_ALPHA;
    pipelineDesc.states.blendState.renderTargets[0].destBlend = Renderer::BlendMode::ONE;

    Renderer::GraphicsPipelineID pipeline = renderer->CreatePipeline(pipelineDesc);

    commandList.BeginPipeline(pipeline);

    _overlayDescriptorSet.Bind("_sampler", params.sampler);
    _overlayDescriptorSet.Bind("_texture", params.overlayImage);
    commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::GLOBAL, &_overlayDescriptorSet, frameIndex);

    struct BlitConstant
    {
        vec4 colorMultiplier;
        vec4 additiveColor;
        u32 channelRedirectors;
    };

    BlitConstant* constants = graphResources.FrameNew<BlitConstant>();
    constants->colorMultiplier = params.colorMultiplier;
    constants->additiveColor = params.additiveColor;

    u32 channelRedirectors = params.channelRedirectors.r;
    channelRedirectors |= (params.channelRedirectors.g << 8);
    channelRedirectors |= (params.channelRedirectors.b << 16);
    channelRedirectors |= (params.channelRedirectors.a << 24);

    constants->channelRedirectors = channelRedirectors;

    commandList.PushConstant(constants, 0, sizeof(BlitConstant));

    commandList.Draw(3, 1, 0, 0);

    commandList.EndPipeline(pipeline);
    commandList.ImageBarrier(params.overlayImage);
    commandList.PopMarker();
}

void RenderUtils::PictureInPicture(Renderer::Renderer* renderer, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList, u32 frameIndex, const PictureInPictureParams& params)
{
    commandList.PushMarker("PictureInPicture", Color::White);
    commandList.ImageBarrier(params.pipImage);

    // Set viewport and scissor
    f32 width = static_cast<f32>(params.targetRegion.right - params.targetRegion.left);
    f32 height = static_cast<f32>(params.targetRegion.bottom - params.targetRegion.top);

    commandList.SetViewport(static_cast<f32>(params.targetRegion.left), static_cast<f32>(params.targetRegion.top), width, height, 0.0f, 1.0f);
    commandList.SetScissorRect(params.targetRegion.left, params.targetRegion.right, params.targetRegion.top, params.targetRegion.bottom);

    Renderer::ImageDesc imageDesc = renderer->GetImageDesc(params.pipImage);

    // Setup pipeline
    Renderer::VertexShaderDesc vertexShaderDesc;
    vertexShaderDesc.path = "Blitting/blit.vs.hlsl";

    Renderer::PixelShaderDesc pixelShaderDesc;
    pixelShaderDesc.path = "Blitting/blit.ps.hlsl";
    std::string texTypeName = GetTexTypeName(imageDesc);
    pixelShaderDesc.AddPermutationField("TEX_TYPE", texTypeName);

    Renderer::GraphicsPipelineDesc pipelineDesc;
    graphResources.InitializePipelineDesc(pipelineDesc);

    pipelineDesc.states.vertexShader = renderer->LoadShader(vertexShaderDesc);
    pipelineDesc.states.pixelShader = renderer->LoadShader(pixelShaderDesc);

    pipelineDesc.renderTargets[0] = params.baseImage;

    pipelineDesc.states.rasterizerState.cullMode = Renderer::CullMode::BACK;
    pipelineDesc.states.rasterizerState.frontFaceMode = Renderer::FrontFaceState::COUNTERCLOCKWISE;

    pipelineDesc.states.blendState.renderTargets[0].blendEnable = false;
    pipelineDesc.states.blendState.renderTargets[0].blendOp = Renderer::BlendOp::ADD;
    pipelineDesc.states.blendState.renderTargets[0].srcBlend = Renderer::BlendMode::ONE;
    pipelineDesc.states.blendState.renderTargets[0].destBlend = Renderer::BlendMode::ZERO;

    Renderer::GraphicsPipelineID pipeline = renderer->CreatePipeline(pipelineDesc);

    commandList.BeginPipeline(pipeline);

    u32 mipLevel = params.mipLevel;
    if (mipLevel >= imageDesc.mipLevels)
    {
        mipLevel = imageDesc.mipLevels - 1;
    }

    _overlayDescriptorSet.Bind("_sampler", params.sampler);
    _overlayDescriptorSet.Bind("_texture", params.pipImage, mipLevel);
    commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::GLOBAL, &_overlayDescriptorSet, frameIndex);

    struct BlitConstant
    {
        vec4 colorMultiplier;
        vec4 additiveColor;
        u32 channelRedirectors;
    };

    BlitConstant* constants = graphResources.FrameNew<BlitConstant>();
    constants->colorMultiplier = params.colorMultiplier;
    constants->additiveColor = params.additiveColor;

    u32 channelRedirectors = params.channelRedirectors.r;
    channelRedirectors |= (params.channelRedirectors.g << 8);
    channelRedirectors |= (params.channelRedirectors.b << 16);
    channelRedirectors |= (params.channelRedirectors.a << 24);

    constants->channelRedirectors = channelRedirectors;

    commandList.PushConstant(constants, 0, sizeof(BlitConstant));

    commandList.Draw(3, 1, 0, 0);

    commandList.EndPipeline(pipeline);
    commandList.ImageBarrier(params.pipImage);

    // Reset the viewport and scissor
    vec2 renderSize = renderer->GetRenderSize();
    commandList.SetViewport(0, 0, renderSize.x, renderSize.y, 0.0f, 1.0f);
    commandList.SetScissorRect(0, static_cast<u32>(renderSize.x), 0, static_cast<u32>(renderSize.y));

    commandList.PopMarker();
}

void RenderUtils::DepthPictureInPicture(Renderer::Renderer* renderer, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList, u32 frameIndex, const DepthPictureInPictureParams& params)
{
    commandList.PushMarker("PictureInPicture", Color::White);
    commandList.ImageBarrier(params.pipImage);

    // Set viewport and scissor
    f32 width = static_cast<f32>(params.targetRegion.right) - static_cast<f32>(params.targetRegion.left);
    f32 height = static_cast<f32>(params.targetRegion.bottom) - static_cast<f32>(params.targetRegion.top);

    commandList.SetViewport(static_cast<f32>(params.targetRegion.left), static_cast<f32>(params.targetRegion.top), width, height, 0.0f, 1.0f);
    commandList.SetScissorRect(params.targetRegion.left, params.targetRegion.right, params.targetRegion.top, params.targetRegion.bottom);

    // Setup pipeline
    Renderer::VertexShaderDesc vertexShaderDesc;
    vertexShaderDesc.path = "Blitting/blit.vs.hlsl";

    Renderer::DepthImageDesc imageDesc = renderer->GetDepthImageDesc(params.pipImage);

    Renderer::ImageComponentType componentType = Renderer::ToImageComponentType(imageDesc.format);
    std::string componentTypeName = "";

    Renderer::PixelShaderDesc pixelShaderDesc;
    pixelShaderDesc.path = "Blitting/blit.ps.hlsl";
    std::string texTypeName = GetTexTypeName(imageDesc);
    pixelShaderDesc.AddPermutationField("TEX_TYPE", texTypeName);

    Renderer::GraphicsPipelineDesc pipelineDesc;
    graphResources.InitializePipelineDesc(pipelineDesc);

    pipelineDesc.states.vertexShader = renderer->LoadShader(vertexShaderDesc);
    pipelineDesc.states.pixelShader = renderer->LoadShader(pixelShaderDesc);

    pipelineDesc.renderTargets[0] = params.baseImage;

    pipelineDesc.states.rasterizerState.cullMode = Renderer::CullMode::BACK;
    pipelineDesc.states.rasterizerState.frontFaceMode = Renderer::FrontFaceState::COUNTERCLOCKWISE;

    pipelineDesc.states.blendState.renderTargets[0].blendEnable = false;
    pipelineDesc.states.blendState.renderTargets[0].blendOp = Renderer::BlendOp::ADD;
    pipelineDesc.states.blendState.renderTargets[0].srcBlend = Renderer::BlendMode::ONE;
    pipelineDesc.states.blendState.renderTargets[0].destBlend = Renderer::BlendMode::ZERO;

    Renderer::GraphicsPipelineID pipeline = renderer->CreatePipeline(pipelineDesc);

    commandList.BeginPipeline(pipeline);

    _overlayDescriptorSet.Bind("_sampler", params.sampler);
    _overlayDescriptorSet.Bind("_texture", params.pipImage);
    commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::GLOBAL, &_overlayDescriptorSet, frameIndex);

    struct BlitConstant
    {
        vec4 colorMultiplier;
        vec4 additiveColor;
        u32 channelRedirectors;
    };

    BlitConstant* constants = graphResources.FrameNew<BlitConstant>();
    constants->colorMultiplier = params.colorMultiplier;
    constants->additiveColor = params.additiveColor;

    u32 channelRedirectors = params.channelRedirectors.r;
    channelRedirectors |= (params.channelRedirectors.g << 8);
    channelRedirectors |= (params.channelRedirectors.b << 16);
    channelRedirectors |= (params.channelRedirectors.a << 24);

    constants->channelRedirectors = channelRedirectors;

    commandList.PushConstant(constants, 0, sizeof(BlitConstant));

    commandList.Draw(3, 1, 0, 0);

    commandList.EndPipeline(pipeline);
    commandList.ImageBarrier(params.pipImage);

    // Reset the viewport and scissor
    vec2 renderSize = renderer->GetRenderSize();
    commandList.SetViewport(0, 0, renderSize.x, renderSize.y, 0.0f, 1.0f);
    commandList.SetScissorRect(0, static_cast<u32>(renderSize.x), 0, static_cast<u32>(renderSize.y));

    commandList.PopMarker();
}

inline u32 GetGroupCount(u32 threadCount, u32 localSize)
{
    return (threadCount + localSize - 1) / localSize;
}

void RenderUtils::CopyDepthToColorRT(Renderer::Renderer* renderer, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList, u32 frameIndex, Renderer::DepthImageID source, Renderer::ImageID destination, u32 destinationMip)
{
    Renderer::ComputePipelineDesc queryPipelineDesc;
    graphResources.InitializePipelineDesc(queryPipelineDesc);

    Renderer::ComputeShaderDesc shaderDesc;
    shaderDesc.path = "Blitting/blitDepth.cs.hlsl";
    queryPipelineDesc.computeShader = renderer->LoadShader(shaderDesc);

    // Do culling
    Renderer::ComputePipelineID pipeline = renderer->CreatePipeline(queryPipelineDesc);
    commandList.BeginPipeline(pipeline);

    commandList.PushMarker("CopyDepthToColorRT", Color::White);

    Renderer::ImageDesc destinationInfo = renderer->GetImageDesc(destination);
    Renderer::DepthImageDesc sourceInfo = renderer->GetDepthImageDesc(source);

    uvec2 destinationSize = renderer->GetImageDimension(destination, destinationMip);

    Renderer::SamplerDesc samplerDesc;
    samplerDesc.filter = Renderer::SamplerFilter::MINIMUM_MIN_MAG_MIP_LINEAR;
    samplerDesc.addressU = Renderer::TextureAddressMode::CLAMP;
    samplerDesc.addressV = Renderer::TextureAddressMode::CLAMP;
    samplerDesc.addressW = Renderer::TextureAddressMode::CLAMP;
    samplerDesc.minLOD = 0.f;
    samplerDesc.maxLOD = 16.f;
    samplerDesc.mode = Renderer::SamplerReductionMode::MIN;

    Renderer::SamplerID occlusionSampler = renderer->CreateSampler(samplerDesc);
    _copyDepthToColorRTDescriptorSet.Bind("_sampler", occlusionSampler);
    _copyDepthToColorRTDescriptorSet.Bind("_source", source);
    _copyDepthToColorRTDescriptorSet.BindStorage("_target", destination, destinationMip);

    struct CopyParams
    {
        glm::vec2 imageSize;
        u32 level;
        u32 dummy;
    };

    CopyParams* copyParams = graphResources.FrameNew<CopyParams>();
    copyParams->imageSize = glm::vec2(destinationSize);
    copyParams->level = destinationMip;

    commandList.PushConstant(copyParams, 0, sizeof(CopyParams));

    commandList.BindDescriptorSet(Renderer::GLOBAL, &_copyDepthToColorRTDescriptorSet, frameIndex);
    commandList.Dispatch(GetGroupCount(destinationSize.x, 32), GetGroupCount(destinationSize.y, 32), 1);

    commandList.EndPipeline(pipeline);

    commandList.ImageBarrier(destination);

    commandList.PopMarker();
}