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
        Renderer::ImageResource input;
        u32 inputMipLevel = 0;
        vec4 colorMultiplier;
        vec4 additiveColor;
        ivec4 channelRedirectors;

        Renderer::ImageMutableResource output;

        Renderer::DescriptorSetResource descriptorSet;
    };
    static void Blit(Renderer::Renderer* renderer, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList, u32 frameIndex, const BlitParams& params);

    struct DepthBlitParams
    {
        Renderer::DepthImageResource input;
        vec4 colorMultiplier;
        vec4 additiveColor;
        ivec4 channelRedirectors;

        Renderer::ImageMutableResource output;
        
        Renderer::DescriptorSetResource descriptorSet;
    };
    static void DepthBlit(Renderer::Renderer* renderer, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList, u32 frameIndex, const DepthBlitParams& params);

    struct OverlayParams
    {
        Renderer::ImageResource overlayImage;
        u32 mipLevel = 0;
        vec4 colorMultiplier;
        vec4 additiveColor;
        ivec4 channelRedirectors;

        Renderer::ImageMutableResource baseImage;

        Renderer::DescriptorSetResource descriptorSet;
    };
    static void Overlay(Renderer::Renderer* renderer, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList, u32 frameIndex, const OverlayParams& params);

    struct DepthOverlayParams
    {
        Renderer::DepthImageResource overlayImage;
        vec4 colorMultiplier;
        vec4 additiveColor;
        ivec4 channelRedirectors;

        Renderer::ImageMutableResource baseImage;

        Renderer::DescriptorSetResource descriptorSet;
    };
    static void DepthOverlay(Renderer::Renderer* renderer, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList, u32 frameIndex, const DepthOverlayParams& params);

    struct PictureInPictureParams
    {
        Renderer::ImageResource pipImage;
        u32 mipLevel = 0;
        vec4 colorMultiplier;
        vec4 additiveColor;
        ivec4 channelRedirectors;
        Geometry::Box targetRegion;

        Renderer::ImageMutableResource baseImage;

        Renderer::DescriptorSetResource descriptorSet;
    };
    static void PictureInPicture(Renderer::Renderer* renderer, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList, u32 frameIndex, const PictureInPictureParams& params);

    struct DepthPictureInPictureParams
    {
        Renderer::DepthImageResource pipImage;
        vec4 colorMultiplier;
        vec4 additiveColor;
        ivec4 channelRedirectors;
        Geometry::Box targetRegion;

        Renderer::ImageMutableResource baseImage;

        Renderer::DescriptorSetResource descriptorSet;
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

    struct CopyDepthToColorParams
    {
        Renderer::DepthImageResource source;
        Renderer::ImageMutableResource destination;
        u32 destinationMip;

        Renderer::DescriptorSetResource descriptorSet;
    };
    static void CopyDepthToColor(Renderer::Renderer* renderer, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList, u32 frameIndex, const CopyDepthToColorParams& params);
private:
};