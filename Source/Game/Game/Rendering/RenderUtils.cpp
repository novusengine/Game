#include "RenderUtils.h"

#include <Renderer/Renderer.h>
#include <Renderer/RenderSettings.h>
#include <Renderer/RenderGraphResources.h>
#include <Renderer/CommandList.h>

#include <Renderer/Descriptors/VertexShaderDesc.h>
#include <Renderer/Descriptors/PixelShaderDesc.h>

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

    const Renderer::ImageDesc& imageDesc = graphResources.GetImageDesc(params.input);

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

    params.descriptorSet.Bind("_texture", params.input, mipLevel);
    commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::GLOBAL, params.descriptorSet, frameIndex);

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
    commandList.PopMarker();
}

void RenderUtils::DepthBlit(Renderer::Renderer* renderer, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList, u32 frameIndex, const DepthBlitParams& params)
{
    commandList.PushMarker("Blit", Color::White);

    const Renderer::DepthImageDesc& imageDesc = graphResources.GetImageDesc(params.input);

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

    params.descriptorSet.Bind("_texture", params.input);
    commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::GLOBAL, params.descriptorSet, frameIndex);

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
    commandList.PopMarker();
}

void RenderUtils::Overlay(Renderer::Renderer* renderer, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList, u32 frameIndex, const OverlayParams& params)
{
    commandList.PushMarker("Overlay", Color::White);

    const Renderer::ImageDesc& imageDesc = graphResources.GetImageDesc(params.overlayImage);

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

    params.descriptorSet.Bind("_texture", params.overlayImage, mipLevel);
    commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::GLOBAL, params.descriptorSet, frameIndex);

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
    commandList.PopMarker();
}

void RenderUtils::DepthOverlay(Renderer::Renderer* renderer, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList, u32 frameIndex, const DepthOverlayParams& params)
{
    commandList.PushMarker("DepthOverlay", Color::White);

    // Setup pipeline
    Renderer::VertexShaderDesc vertexShaderDesc;
    vertexShaderDesc.path = "Blitting/blit.vs.hlsl";

    const Renderer::DepthImageDesc& imageDesc = graphResources.GetImageDesc(params.overlayImage);

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

    params.descriptorSet.Bind("_texture", params.overlayImage);
    commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::GLOBAL, params.descriptorSet, frameIndex);

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
    commandList.PopMarker();
}

void RenderUtils::PictureInPicture(Renderer::Renderer* renderer, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList, u32 frameIndex, const PictureInPictureParams& params)
{
    commandList.PushMarker("PictureInPicture", Color::White);

    // Set viewport and scissor
    f32 width = static_cast<f32>(params.targetRegion.right - params.targetRegion.left);
    f32 height = static_cast<f32>(params.targetRegion.bottom - params.targetRegion.top);

    commandList.SetViewport(static_cast<f32>(params.targetRegion.left), static_cast<f32>(params.targetRegion.top), width, height, 0.0f, 1.0f);
    commandList.SetScissorRect(params.targetRegion.left, params.targetRegion.right, params.targetRegion.top, params.targetRegion.bottom);

    const Renderer::ImageDesc& imageDesc = graphResources.GetImageDesc(params.pipImage);

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

    params.descriptorSet.Bind("_texture", params.pipImage, mipLevel);
    commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::GLOBAL, params.descriptorSet, frameIndex);

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

    // Reset the viewport and scissor
    vec2 renderSize = renderer->GetRenderSize();
    commandList.SetViewport(0, 0, renderSize.x, renderSize.y, 0.0f, 1.0f);
    commandList.SetScissorRect(0, static_cast<u32>(renderSize.x), 0, static_cast<u32>(renderSize.y));

    commandList.PopMarker();
}

void RenderUtils::DepthPictureInPicture(Renderer::Renderer* renderer, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList, u32 frameIndex, const DepthPictureInPictureParams& params)
{
    commandList.PushMarker("PictureInPicture", Color::White);

    // Set viewport and scissor
    f32 width = static_cast<f32>(params.targetRegion.right) - static_cast<f32>(params.targetRegion.left);
    f32 height = static_cast<f32>(params.targetRegion.bottom) - static_cast<f32>(params.targetRegion.top);

    commandList.SetViewport(static_cast<f32>(params.targetRegion.left), static_cast<f32>(params.targetRegion.top), width, height, 0.0f, 1.0f);
    commandList.SetScissorRect(params.targetRegion.left, params.targetRegion.right, params.targetRegion.top, params.targetRegion.bottom);

    // Setup pipeline
    Renderer::VertexShaderDesc vertexShaderDesc;
    vertexShaderDesc.path = "Blitting/blit.vs.hlsl";

    const Renderer::DepthImageDesc& imageDesc = graphResources.GetImageDesc(params.pipImage);

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

    params.descriptorSet.Bind("_texture", params.pipImage);
    commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::GLOBAL, params.descriptorSet, frameIndex);

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

    // Reset the viewport and scissor
    vec2 renderSize = renderer->GetRenderSize();
    commandList.SetViewport(0, 0, renderSize.x, renderSize.y, 0.0f, 1.0f);
    commandList.SetScissorRect(0, static_cast<u32>(renderSize.x), 0, static_cast<u32>(renderSize.y));

    commandList.PopMarker();
}

void RenderUtils::CopyDepthToColor(Renderer::Renderer* renderer, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList, u32 frameIndex, const CopyDepthToColorParams& params)
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

    Renderer::ImageDesc destinationInfo = graphResources.GetImageDesc(params.destination);
    Renderer::DepthImageDesc sourceInfo = graphResources.GetImageDesc(params.source);

    uvec2 destinationSize = graphResources.GetImageDimensions(params.destination, params.destinationMip);

    params.descriptorSet.Bind("_source", params.source);
    params.descriptorSet.BindStorage("_target", params.destination, params.destinationMip);

    struct CopyParams
    {
        glm::vec2 imageSize;
        u32 level;
        u32 dummy;
    };

    CopyParams* copyParams = graphResources.FrameNew<CopyParams>();
    copyParams->imageSize = glm::vec2(destinationSize);
    copyParams->level = params.destinationMip;

    commandList.PushConstant(copyParams, 0, sizeof(CopyParams));

    commandList.BindDescriptorSet(Renderer::GLOBAL, params.descriptorSet, frameIndex);
    commandList.Dispatch(GetGroupCount(destinationSize.x, 32), GetGroupCount(destinationSize.y, 32), 1);

    commandList.EndPipeline(pipeline);

    commandList.PopMarker();
}