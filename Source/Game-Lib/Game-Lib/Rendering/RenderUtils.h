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
class GameRenderer;

class RenderUtils
{
public:
    static void Init(Renderer::Renderer* renderer, GameRenderer* gameRenderer);

    struct BlitParams
    {
    public:
        Renderer::ImageResource input;
        u32 inputMipLevel = 0;
        vec4 colorMultiplier = vec4(1, 1, 1, 1);
        vec4 additiveColor = vec4(0, 0, 0, 0);
        ivec4 channelRedirectors = ivec4(0, 1, 2, 3);

        Renderer::ImageMutableResource output;

        Renderer::DescriptorSetResource descriptorSet;
    };
    static void Blit(Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList, u32 frameIndex, const BlitParams& params);

    struct DepthBlitParams
    {
    public:
        Renderer::DepthImageResource input;
        vec4 colorMultiplier = vec4(1, 1, 1, 1);
        vec4 additiveColor = vec4(0, 0, 0, 0);
        ivec4 channelRedirectors = ivec4(0, 1, 2, 3);

        Renderer::ImageMutableResource output;
        
        Renderer::DescriptorSetResource descriptorSet;
    };
    static void DepthBlit(Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList, u32 frameIndex, const DepthBlitParams& params);

    struct OverlayParams
    {
    public:
        Renderer::ImageResource overlayImage;
        u32 mipLevel = 0;
        vec4 colorMultiplier = vec4(1,1,1,1);
        vec4 additiveColor = vec4(0,0,0,0);
        ivec4 channelRedirectors = ivec4(0, 1, 2, 3);

        Renderer::ImageMutableResource baseImage;

        Renderer::DescriptorSetResource descriptorSet;
    };
    static void Overlay(Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList, u32 frameIndex, const OverlayParams& params);

    struct DepthOverlayParams
    {
    public:
        Renderer::DepthImageResource overlayImage;
        vec4 colorMultiplier = vec4(1, 1, 1, 1);
        vec4 additiveColor = vec4(0, 0, 0, 0);
        ivec4 channelRedirectors = ivec4(0, 1, 2, 3);

        Renderer::ImageMutableResource baseImage;

        Renderer::DescriptorSetResource descriptorSet;
    };
    static void DepthOverlay(Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList, u32 frameIndex, const DepthOverlayParams& params);

    struct PictureInPictureParams
    {
    public:
        Renderer::ImageResource pipImage;
        u32 mipLevel = 0;
        vec4 colorMultiplier = vec4(1, 1, 1, 1);
        vec4 additiveColor = vec4(0, 0, 0, 0);
        ivec4 channelRedirectors = ivec4(0, 1, 2, 3);
        Geometry::Box targetRegion;

        Renderer::ImageMutableResource baseImage;

        Renderer::DescriptorSetResource descriptorSet;
    };
    static void PictureInPicture(Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList, u32 frameIndex, const PictureInPictureParams& params);

    struct DepthPictureInPictureParams
    {
    public:
        Renderer::DepthImageResource pipImage;
        vec4 colorMultiplier = vec4(1, 1, 1, 1);
        vec4 additiveColor = vec4(0, 0, 0, 0);
        ivec4 channelRedirectors = ivec4(0, 1, 2, 3);
        Geometry::Box targetRegion;

        Renderer::ImageMutableResource baseImage;

        Renderer::DescriptorSetResource descriptorSet;
    };
    static void DepthPictureInPicture(Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList, u32 frameIndex, const DepthPictureInPictureParams& params);

    static u32 CalcCullingBitmaskSize(size_t numObjects)
    {
        u32 numBytesNeeded = static_cast<u32>(((numObjects + 7) / 8));

        // We store these as uints, so we have to pad it to the next multiple of 4 as well
        numBytesNeeded = Math::NextMultipleOf(numBytesNeeded, 4);

        return numBytesNeeded;
    }

    static u32 CalcCullingBitmaskUints(size_t numObjects)
    {
        u32 numUintsNeeded = static_cast<u32>(((numObjects + 31) / 32));

        return numUintsNeeded;
    }

    static u32 GetGroupCount(u32 threadCount, u32 localSize)
    {
        return (threadCount + localSize - 1) / localSize;
    }

    struct CopyDepthToColorParams
    {
    public:
        Renderer::DepthImageResource source;
        Renderer::ImageMutableResource destination;
        u32 destinationMip;

        Renderer::DescriptorSetResource descriptorSet;
    };
    static void CopyDepthToColor(Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList, u32 frameIndex, const CopyDepthToColorParams& params);

    static Renderer::ComputePipelineID GetCopyDepthToColorPipeline() { return _copyDepthToColorPipeline; }
private:
    static Renderer::Renderer* _renderer;
    static GameRenderer* _gameRenderer;

    static Renderer::ComputePipelineID _copyDepthToColorPipeline;
};