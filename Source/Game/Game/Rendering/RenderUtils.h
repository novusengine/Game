#pragma once
#include <Base/Types.h>
#include <Base/Math/Geometry.h>

#include <Renderer/RenderGraphResources.h>
#include <Renderer/Descriptors/ImageDesc.h>
#include <Renderer/Descriptors/SamplerDesc.h>
#include <Renderer/DescriptorSet.h>

namespace Renderer
{
    class Renderer;
    class RenderGraphResources;
    class CommandList;
}

class RenderUtils
{
public:
    struct BlitParams
    {
        Renderer::ImageID input;
        u32 inputMipLevel = 0;
        vec4 colorMultiplier;
        vec4 additiveColor;
        ivec4 channelRedirectors;

        Renderer::RenderPassMutableResource output;
        Renderer::SamplerID sampler;
    };
    static void Blit(Renderer::Renderer* renderer, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList, u32 frameIndex, const BlitParams& params);

    struct DepthBlitParams
    {
        Renderer::DepthImageID input;
        vec4 colorMultiplier;
        vec4 additiveColor;
        ivec4 channelRedirectors;

        Renderer::RenderPassMutableResource output;
        Renderer::SamplerID sampler;
    };
    static void DepthBlit(Renderer::Renderer* renderer, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList, u32 frameIndex, const DepthBlitParams& params);

    struct OverlayParams
    {
        Renderer::ImageID overlayImage;
        u32 mipLevel = 0;
        vec4 colorMultiplier;
        vec4 additiveColor;
        ivec4 channelRedirectors;

        Renderer::RenderPassMutableResource baseImage;
        Renderer::SamplerID sampler;
    };
    static void Overlay(Renderer::Renderer* renderer, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList, u32 frameIndex, const OverlayParams& params);

    struct DepthOverlayParams
    {
        Renderer::DepthImageID overlayImage;
        vec4 colorMultiplier;
        vec4 additiveColor;
        ivec4 channelRedirectors;

        Renderer::RenderPassMutableResource baseImage;
        Renderer::SamplerID sampler;
    };
    static void DepthOverlay(Renderer::Renderer* renderer, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList, u32 frameIndex, const DepthOverlayParams& params);

    struct PictureInPictureParams
    {
        Renderer::ImageID pipImage;
        u32 mipLevel = 0;
        vec4 colorMultiplier;
        vec4 additiveColor;
        ivec4 channelRedirectors;
        Geometry::Box targetRegion;

        Renderer::RenderPassMutableResource baseImage;
        Renderer::SamplerID sampler;
    };
    static void PictureInPicture(Renderer::Renderer* renderer, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList, u32 frameIndex, const PictureInPictureParams& params);

    struct DepthPictureInPictureParams
    {
        Renderer::DepthImageID pipImage;
        vec4 colorMultiplier;
        vec4 additiveColor;
        ivec4 channelRedirectors;
        Geometry::Box targetRegion;

        Renderer::RenderPassMutableResource baseImage;
        Renderer::SamplerID sampler;
    };
    static void DepthPictureInPicture(Renderer::Renderer* renderer, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList, u32 frameIndex, const DepthPictureInPictureParams& params);

    static u32 CalcCullingBitmaskSize(size_t numObjects)
    {
        u32 numBytesNeeded = static_cast<u32>(((numObjects + 7) / 8));

        // We store these as uints, so we have to pad it to the next multiple of 4 as well
        numBytesNeeded = Math::NextMultipleOf(numBytesNeeded, 4);

        return numBytesNeeded;
    }

    static u32 GetGroupCount(u32 threadCount, u32 localSize)
    {
        return (threadCount + localSize - 1) / localSize;
    }

    static void CopyDepthToColorRT(Renderer::Renderer* renderer, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList, u32 frameIndex, Renderer::DepthImageID source, Renderer::ImageID destination, u32 destinationMip);
private:

private:
    static Renderer::DescriptorSet _overlayDescriptorSet;
    static Renderer::DescriptorSet _copyDepthToColorRTDescriptorSet;
};