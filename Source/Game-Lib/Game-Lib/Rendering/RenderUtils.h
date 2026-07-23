#pragma once
#include <Base/Types.h>
#include <Base/Math/Geometry.h>

#include <Renderer/RenderGraphResources.h>
#include <Renderer/Descriptors/ImageDesc.h>
#include <Renderer/Descriptors/SamplerDesc.h>
#include <Renderer/Descriptors/TimeQueryDesc.h>
#include <Renderer/DescriptorSet.h>

#include <string>

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

    // svsmProfileGeometry: wraps a per-view SVSM fill/draw stage in a named GPU time query so it
    // surfaces in the perf editor's render pass list. Off by default, queries around tiny
    // dispatches serialize slightly
    class SVSMGeometryProfiler
    {
    public:
        SVSMGeometryProfiler(Renderer::Renderer* renderer, Renderer::CommandList& commandList, std::string namePrefix, bool enabled)
            : _renderer(renderer)
            , _commandList(&commandList)
            , _namePrefix(std::move(namePrefix))
            , _enabled(enabled)
        {
        }

        template <typename Work>
        void operator()(const char* stageName, u32 viewIndex, Work&& work) const
        {
            if (!_enabled)
            {
                work();
                return;
            }

            Renderer::TimeQueryID timeQuery = BeginStage(stageName, viewIndex);
            work();
            EndStage(timeQuery);
        }

    private:
        Renderer::TimeQueryID BeginStage(const char* stageName, u32 viewIndex) const;
        void EndStage(Renderer::TimeQueryID timeQuery) const;

        Renderer::Renderer* _renderer = nullptr;
        Renderer::CommandList* _commandList = nullptr;
        std::string _namePrefix;
        bool _enabled = false;
    };

    // One draw per SVSM clip rect (new X columns, new Y rows, other): the vertex shader clips
    // fragments to the rect, so an L-shaped toroidal update rasterizes two thin stripes instead
    // of its whole-window bounding box. svsmClipRects 0: single draw, the rect index stays at the
    // disabled sentinel
    template <typename DrawParamsT, typename DrawFn>
    static void DrawSVSMClipRects(bool clipRectsEnabled, DrawParamsT& drawParams, DrawFn&& draw)
    {
        if (clipRectsEnabled)
        {
            for (u32 rectIndex = 0; rectIndex < 3; rectIndex++)
            {
                drawParams.svsmRectIndex = rectIndex;
                draw(drawParams);
            }
        }
        else
        {
            draw(drawParams);
        }
    }
private:
    static Renderer::Renderer* _renderer;
    static GameRenderer* _gameRenderer;

    static Renderer::ComputePipelineID _copyDepthToColorPipeline;
};