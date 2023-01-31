#pragma once
#include <Base/Types.h>

#include <Renderer/Descriptors/DepthImageDesc.h>
#include <Renderer/Descriptors/ImageDesc.h>
#include <Renderer/DescriptorSet.h>
#include "RenderResources.h"

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
	static void BuildPyramid(Renderer::Renderer* renderer, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList, RenderResources& resources, u32 frameIndex);
	static void BuildPyramid2(Renderer::Renderer* renderer, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList, RenderResources& resources, u32 frameIndex);

	static Renderer::SamplerID _copySampler;
	static Renderer::SamplerID _pyramidSampler;
	static Renderer::DescriptorSet _copyDescriptorSet;
	static Renderer::DescriptorSet _pyramidDescriptorSet;
	static Renderer::BufferID _atomicBuffer;
};