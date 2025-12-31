#include "RenderUtils.h"
#include "Game-Lib/Rendering/GameRenderer.h"

#include <Renderer/Renderer.h>
#include <Renderer/RenderSettings.h>
#include <Renderer/RenderStates.h>
#include <Renderer/RenderGraphResources.h>
#include <Renderer/CommandList.h>

#include <Renderer/Descriptors/VertexShaderDesc.h>
#include <Renderer/Descriptors/PixelShaderDesc.h>

Renderer::Renderer* RenderUtils::_renderer = nullptr;
GameRenderer* RenderUtils::_gameRenderer = nullptr;

Renderer::ComputePipelineID RenderUtils::_copyDepthToColorPipeline;

void RenderUtils::Init(Renderer::Renderer* renderer, GameRenderer* gameRenderer)
{
    _renderer = renderer;
    _gameRenderer = gameRenderer;

    // Create pipelines
    {
        Renderer::ComputePipelineDesc pipelineDesc;
        pipelineDesc.debugName = "CopyDepthToColor";

        Renderer::ComputeShaderDesc shaderDesc;
        shaderDesc.shaderEntry = _gameRenderer->GetShaderEntry("Blitting/BlitDepth.cs"_h, "Blitting/BlitDepth.cs");
        pipelineDesc.computeShader = _renderer->LoadShader(shaderDesc);

        _copyDepthToColorPipeline = _renderer->CreatePipeline(pipelineDesc);
    }
}

void RenderUtils::Blit(Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList, u32 frameIndex, const BlitParams& params)
{
    commandList.PushMarker("Blit", Color::White);

    Renderer::RenderPassDesc renderPassDesc;
    graphResources.InitializeRenderPassDesc(renderPassDesc);

    // Render targets
    renderPassDesc.renderTargets[0] = params.output;
    commandList.BeginRenderPass(renderPassDesc);

    const Renderer::ImageDesc& imageDesc = graphResources.GetImageDesc(params.input);

    std::string componentTypeName = GetTextureTypeName(imageDesc.format);
    u32 componentTypeNameHash = StringUtils::fnv1a_32(componentTypeName.c_str(), componentTypeName.size());
    Renderer::GraphicsPipelineID pipeline = _gameRenderer->GetBlitPipeline(componentTypeNameHash);

    commandList.BeginPipeline(pipeline);

    u32 mipLevel = params.inputMipLevel;
    if (mipLevel >= imageDesc.mipLevels)
    {
        mipLevel = imageDesc.mipLevels - 1;
    }

    params.descriptorSet.Bind("_texture", params.input, mipLevel);
    commandList.BindDescriptorSet(params.descriptorSet, frameIndex);

    struct BlitConstant
    {
        vec4 colorMultiplier;
        vec4 additiveColor;
        vec4 uvOffsetAndExtent;
        u32 channelRedirectors;
    };

    BlitConstant* constants = graphResources.FrameNew<BlitConstant>();
    constants->colorMultiplier = params.colorMultiplier;
    constants->additiveColor = params.additiveColor;
    constants->uvOffsetAndExtent = vec4(0.0f, 0.0f, 1.0f, 1.0f);

    u32 channelRedirectors = params.channelRedirectors.r;
    channelRedirectors |= (params.channelRedirectors.g << 8);
    channelRedirectors |= (params.channelRedirectors.b << 16);
    channelRedirectors |= (params.channelRedirectors.a << 24);

    constants->channelRedirectors = channelRedirectors;

    commandList.PushConstant(constants, 0, sizeof(BlitConstant));

    commandList.Draw(3, 1, 0, 0);

    commandList.EndPipeline(pipeline);
    commandList.EndRenderPass(renderPassDesc);
    commandList.PopMarker();
}

void RenderUtils::DepthBlit(Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList, u32 frameIndex, const DepthBlitParams& params)
{
    commandList.PushMarker("Blit", Color::White);

    Renderer::RenderPassDesc renderPassDesc;
    graphResources.InitializeRenderPassDesc(renderPassDesc);

    // Render targets
    renderPassDesc.renderTargets[0] = params.output;
    commandList.BeginRenderPass(renderPassDesc);

    const Renderer::DepthImageDesc& imageDesc = graphResources.GetImageDesc(params.input);

    std::string componentTypeName = GetTextureTypeName(imageDesc.format);
    u32 componentTypeNameHash = StringUtils::fnv1a_32(componentTypeName.c_str(), componentTypeName.size());
    Renderer::GraphicsPipelineID pipeline = _gameRenderer->GetBlitPipeline(componentTypeNameHash);
    commandList.BeginPipeline(pipeline);

    params.descriptorSet.Bind("_texture", params.input);
    commandList.BindDescriptorSet(params.descriptorSet, frameIndex);

    struct BlitConstant
    {
        vec4 colorMultiplier;
        vec4 additiveColor;
        vec4 uvOffsetAndExtent;
        u32 channelRedirectors;
    };

    BlitConstant* constants = graphResources.FrameNew<BlitConstant>();
    constants->colorMultiplier = params.colorMultiplier;
    constants->additiveColor = params.additiveColor;
    constants->uvOffsetAndExtent = vec4(0.0f, 0.0f, 1.0f, 1.0f);

    u32 channelRedirectors = params.channelRedirectors.r;
    channelRedirectors |= (params.channelRedirectors.g << 8);
    channelRedirectors |= (params.channelRedirectors.b << 16);
    channelRedirectors |= (params.channelRedirectors.a << 24);

    constants->channelRedirectors = channelRedirectors;

    commandList.PushConstant(constants, 0, sizeof(BlitConstant));

    commandList.Draw(3, 1, 0, 0);

    commandList.EndPipeline(pipeline);
    commandList.EndRenderPass(renderPassDesc);
    commandList.PopMarker();
}

void RenderUtils::Overlay(Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList, u32 frameIndex, const OverlayParams& params)
{
    commandList.PushMarker("Overlay", Color::White);

    Renderer::RenderPassDesc renderPassDesc;
    graphResources.InitializeRenderPassDesc(renderPassDesc);

    // Render targets
    renderPassDesc.renderTargets[0] = params.baseImage;
    commandList.BeginRenderPass(renderPassDesc);

    const Renderer::ImageDesc& imageDesc = graphResources.GetImageDesc(params.overlayImage);

    std::string componentTypeName = GetTextureTypeName(imageDesc.format);
    u32 componentTypeNameHash = StringUtils::fnv1a_32(componentTypeName.c_str(), componentTypeName.size());
    Renderer::GraphicsPipelineID pipeline = _gameRenderer->GetOverlayPipeline(componentTypeNameHash);

    commandList.BeginPipeline(pipeline);

    u32 mipLevel = params.mipLevel;
    if (mipLevel >= imageDesc.mipLevels)
    {
        mipLevel = imageDesc.mipLevels - 1;
    }

    params.descriptorSet.Bind("_texture", params.overlayImage, mipLevel);
    commandList.BindDescriptorSet(params.descriptorSet, frameIndex);

    struct BlitConstant
    {
        vec4 colorMultiplier;
        vec4 additiveColor;
        vec4 uvOffsetAndExtent;
        u32 channelRedirectors;
    };

    BlitConstant* constants = graphResources.FrameNew<BlitConstant>();
    constants->colorMultiplier = params.colorMultiplier;
    constants->additiveColor = params.additiveColor;
    constants->uvOffsetAndExtent = vec4(0.0f, 0.0f, 1.0f, 1.0f);

    u32 channelRedirectors = params.channelRedirectors.r;
    channelRedirectors |= (params.channelRedirectors.g << 8);
    channelRedirectors |= (params.channelRedirectors.b << 16);
    channelRedirectors |= (params.channelRedirectors.a << 24);

    constants->channelRedirectors = channelRedirectors;

    commandList.PushConstant(constants, 0, sizeof(BlitConstant));

    commandList.Draw(3, 1, 0, 0);

    commandList.EndPipeline(pipeline);
    commandList.EndRenderPass(renderPassDesc);
    commandList.PopMarker();
}

void RenderUtils::DepthOverlay(Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList, u32 frameIndex, const DepthOverlayParams& params)
{
    commandList.PushMarker("DepthOverlay", Color::White);

    Renderer::RenderPassDesc renderPassDesc;
    graphResources.InitializeRenderPassDesc(renderPassDesc);

    // Render targets
    renderPassDesc.renderTargets[0] = params.baseImage;
    commandList.BeginRenderPass(renderPassDesc);

    const Renderer::DepthImageDesc& imageDesc = graphResources.GetImageDesc(params.overlayImage);

    std::string componentTypeName = GetTextureTypeName(imageDesc.format);
    u32 componentTypeNameHash = StringUtils::fnv1a_32(componentTypeName.c_str(), componentTypeName.size());
    Renderer::GraphicsPipelineID pipeline = _gameRenderer->GetOverlayPipeline(componentTypeNameHash);

    commandList.BeginPipeline(pipeline);

    params.descriptorSet.Bind("_texture", params.overlayImage);
    commandList.BindDescriptorSet(params.descriptorSet, frameIndex);

    struct BlitConstant
    {
        vec4 colorMultiplier;
        vec4 additiveColor;
        vec4 uvOffsetAndExtent;
        u32 channelRedirectors;
    };

    BlitConstant* constants = graphResources.FrameNew<BlitConstant>();
    constants->colorMultiplier = params.colorMultiplier;
    constants->additiveColor = params.additiveColor;
    constants->uvOffsetAndExtent = vec4(0.0f, 0.0f, 1.0f, 1.0f);

    u32 channelRedirectors = params.channelRedirectors.r;
    channelRedirectors |= (params.channelRedirectors.g << 8);
    channelRedirectors |= (params.channelRedirectors.b << 16);
    channelRedirectors |= (params.channelRedirectors.a << 24);

    constants->channelRedirectors = channelRedirectors;

    commandList.PushConstant(constants, 0, sizeof(BlitConstant));

    commandList.Draw(3, 1, 0, 0);

    commandList.EndPipeline(pipeline);
    commandList.EndRenderPass(renderPassDesc);
    commandList.PopMarker();
}

void RenderUtils::PictureInPicture(Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList, u32 frameIndex, const PictureInPictureParams& params)
{
    commandList.PushMarker("PictureInPicture", Color::White);

    Renderer::RenderPassDesc renderPassDesc;
    graphResources.InitializeRenderPassDesc(renderPassDesc);

    // Render targets
    renderPassDesc.renderTargets[0] = params.baseImage;
    commandList.BeginRenderPass(renderPassDesc); // Maybe this could use offset and size instead of viewport and scissorregion?

    // Set viewport and scissor
    f32 width = static_cast<f32>(params.targetRegion.right - params.targetRegion.left);
    f32 height = static_cast<f32>(params.targetRegion.bottom - params.targetRegion.top);

    commandList.SetViewport(static_cast<f32>(params.targetRegion.left), static_cast<f32>(params.targetRegion.top), width, height, 0.0f, 1.0f);
    commandList.SetScissorRect(params.targetRegion.left, params.targetRegion.right, params.targetRegion.top, params.targetRegion.bottom);

    const Renderer::ImageDesc& imageDesc = graphResources.GetImageDesc(params.pipImage);

    std::string componentTypeName = GetTextureTypeName(imageDesc.format);
    u32 componentTypeNameHash = StringUtils::fnv1a_32(componentTypeName.c_str(), componentTypeName.size());
    Renderer::GraphicsPipelineID pipeline = _gameRenderer->GetBlitPipeline(componentTypeNameHash);

    commandList.BeginPipeline(pipeline);

    u32 mipLevel = params.mipLevel;
    if (mipLevel >= imageDesc.mipLevels)
    {
        mipLevel = imageDesc.mipLevels - 1;
    }

    params.descriptorSet.Bind("_texture", params.pipImage, mipLevel);
    commandList.BindDescriptorSet(params.descriptorSet, frameIndex);

    struct BlitConstant
    {
        vec4 colorMultiplier;
        vec4 additiveColor;
        vec4 uvOffsetAndExtent;
        u32 channelRedirectors;
    };

    BlitConstant* constants = graphResources.FrameNew<BlitConstant>();
    constants->colorMultiplier = params.colorMultiplier;
    constants->additiveColor = params.additiveColor;
    constants->uvOffsetAndExtent = vec4(0.0f, 0.0f, 1.0f, 1.0f);

    u32 channelRedirectors = params.channelRedirectors.r;
    channelRedirectors |= (params.channelRedirectors.g << 8);
    channelRedirectors |= (params.channelRedirectors.b << 16);
    channelRedirectors |= (params.channelRedirectors.a << 24);

    constants->channelRedirectors = channelRedirectors;

    commandList.PushConstant(constants, 0, sizeof(BlitConstant));

    commandList.Draw(3, 1, 0, 0);

    commandList.EndPipeline(pipeline);
    commandList.EndRenderPass(renderPassDesc);

    // Reset the viewport and scissor
    vec2 renderSize = _renderer->GetRenderSize();
    commandList.SetViewport(0, 0, renderSize.x, renderSize.y, 0.0f, 1.0f);
    commandList.SetScissorRect(0, static_cast<u32>(renderSize.x), 0, static_cast<u32>(renderSize.y));

    commandList.PopMarker();
}

void RenderUtils::DepthPictureInPicture(Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList, u32 frameIndex, const DepthPictureInPictureParams& params)
{
    commandList.PushMarker("DepthPictureInPicture", Color::White);

    Renderer::RenderPassDesc renderPassDesc;
    graphResources.InitializeRenderPassDesc(renderPassDesc);

    // Render targets
    renderPassDesc.renderTargets[0] = params.baseImage;
    commandList.BeginRenderPass(renderPassDesc);

    // Set viewport and scissor
    f32 width = static_cast<f32>(params.targetRegion.right) - static_cast<f32>(params.targetRegion.left);
    f32 height = static_cast<f32>(params.targetRegion.bottom) - static_cast<f32>(params.targetRegion.top);

    commandList.SetViewport(static_cast<f32>(params.targetRegion.left), static_cast<f32>(params.targetRegion.top), width, height, 0.0f, 1.0f);
    commandList.SetScissorRect(params.targetRegion.left, params.targetRegion.right, params.targetRegion.top, params.targetRegion.bottom);

    const Renderer::DepthImageDesc& imageDesc = graphResources.GetImageDesc(params.pipImage);

    std::string componentTypeName = GetTextureTypeName(imageDesc.format);
    u32 componentTypeNameHash = StringUtils::fnv1a_32(componentTypeName.c_str(), componentTypeName.size());
    Renderer::GraphicsPipelineID pipeline = _gameRenderer->GetBlitPipeline(componentTypeNameHash);

    commandList.BeginPipeline(pipeline);

    params.descriptorSet.Bind("_texture", params.pipImage);
    commandList.BindDescriptorSet(params.descriptorSet, frameIndex);

    struct BlitConstant
    {
        vec4 colorMultiplier;
        vec4 additiveColor;
        vec4 uvOffsetAndExtent;
        u32 channelRedirectors;
    };

    BlitConstant* constants = graphResources.FrameNew<BlitConstant>();
    constants->colorMultiplier = params.colorMultiplier;
    constants->additiveColor = params.additiveColor;
    constants->uvOffsetAndExtent = vec4(0.0f, 0.0f, 1.0f, 1.0f);

    u32 channelRedirectors = params.channelRedirectors.r;
    channelRedirectors |= (params.channelRedirectors.g << 8);
    channelRedirectors |= (params.channelRedirectors.b << 16);
    channelRedirectors |= (params.channelRedirectors.a << 24);

    constants->channelRedirectors = channelRedirectors;

    commandList.PushConstant(constants, 0, sizeof(BlitConstant));

    commandList.Draw(3, 1, 0, 0);

    commandList.EndPipeline(pipeline);
    commandList.EndRenderPass(renderPassDesc);

    // Reset the viewport and scissor
    vec2 renderSize = _renderer->GetRenderSize();
    commandList.SetViewport(0, 0, renderSize.x, renderSize.y, 0.0f, 1.0f);
    commandList.SetScissorRect(0, static_cast<u32>(renderSize.x), 0, static_cast<u32>(renderSize.y));

    commandList.PopMarker();
}

void RenderUtils::CopyDepthToColor(Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList, u32 frameIndex, const CopyDepthToColorParams& params)
{
    Renderer::ComputePipelineID pipeline = _copyDepthToColorPipeline;
    commandList.BeginPipeline(pipeline);

    commandList.PushMarker("CopyDepthToColorRT", Color::White);

    Renderer::ImageDesc destinationInfo = graphResources.GetImageDesc(params.destination);
    Renderer::DepthImageDesc sourceInfo = graphResources.GetImageDesc(params.source);

    uvec2 destinationSize = graphResources.GetImageDimensions(params.destination, params.destinationMip);

    params.descriptorSet.Bind("_source", params.source);
    params.descriptorSet.Bind("_target", params.destination, params.destinationMip);

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

    commandList.BindDescriptorSet(params.descriptorSet, frameIndex);
    commandList.Dispatch(GetGroupCount(destinationSize.x, 32), GetGroupCount(destinationSize.y, 32), 1);

    commandList.EndPipeline(pipeline);

    commandList.PopMarker();
}