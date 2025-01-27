#pragma once
#include "RenderResources.h"

#include <Base/Types.h>

#include <Renderer/Descriptors/DepthImageDesc.h>
#include <Renderer/Descriptors/ImageDesc.h>
#include <Renderer/DescriptorSet.h>
#include <Renderer/DescriptorSetResource.h>

namespace Renderer
{
    class Renderer;
    class RenderGraphResources;
    class CommandList;
}
class DepthPyramidUtils
{
public:
    static void InitBuffers(Renderer::Renderer* renderer);

    struct BuildPyramidParams
    {
    public:
        Renderer::Renderer* renderer;
        Renderer::RenderGraphResources* graphResources;
        Renderer::CommandList* commandList;
        RenderResources* resources;
        u32 frameIndex;

        uvec2 pyramidSize;
        Renderer::DepthImageResource depth;
        Renderer::ImageMutableResource depthPyramid;

        Renderer::DescriptorSetResource copyDescriptorSet;
        Renderer::DescriptorSetResource pyramidDescriptorSet;
    };
    static void BuildPyramid(BuildPyramidParams& params);

    static Renderer::SamplerID _copySampler;
    static Renderer::SamplerID _pyramidSampler;
    static Renderer::DescriptorSet _copyDescriptorSet;
    static Renderer::DescriptorSet _pyramidDescriptorSet;
    static Renderer::BufferID _atomicBuffer;
};