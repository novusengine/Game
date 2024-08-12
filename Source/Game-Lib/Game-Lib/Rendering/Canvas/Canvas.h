#pragma once
#include <Base/Types.h>

#include <Renderer/Descriptors/ImageDesc.h>
#include <Renderer/Descriptors/TextureDesc.h>
#include <Renderer/DescriptorSetResource.h>
#include <Renderer/GPUVector.h>

#include <shared_mutex>

namespace Renderer
{
	class Renderer;
	class CommandList;
	class RenderGraphBuilder;
    struct Font;
}

STRONG_TYPEDEF(CanvasTextureID, Renderer::TextureID::type);

struct CanvasTextureDrawData
{
	uvec4 positionAndSize; // Pixels
	ivec4 textureIndex;
	uvec4 slicingOffset; // Pixels
};

struct CanvasTextVertex
{
	vec4 posAndUV;
};

struct CanvasCharDrawData
{
	ivec4 data; // x = textureID, y = textColor, z = outlineColor, w = (float)outlineWidth
};

struct CanvasLayer
{
	Renderer::GPUVector<CanvasTextureDrawData> textureDrawData;

	Renderer::GPUVector<CanvasTextVertex> textVertices;
	Renderer::GPUVector<CanvasCharDrawData> charDrawData;

	Renderer::BufferResource textureDrawDataResource;
	Renderer::BufferResource textVerticesResource;
	Renderer::BufferResource charDrawDataResource;
};

class Canvas
{
public:
	Canvas(Renderer::Renderer* renderer, Renderer::ImageID renderTarget);

    void DrawText(Renderer::Font& font, const std::string& text, const vec2& pos, i8 layer = 0);
    void DrawTexture(CanvasTextureID textureID, const vec2& pos, const vec2& size, const uvec4& slicingOffsetPixels, i8 layer = 0);

private:
	void Setup(Renderer::RenderGraphBuilder& builder);

	struct DrawParams
	{
		DrawParams(Renderer::RenderGraphResources& graphResources, 
			Renderer::CommandList& commandList,
			Renderer::DescriptorSetResource& globalSet, 
			Renderer::DescriptorSetResource& texturesSet, 
			Renderer::DescriptorSetResource& textSet, 
			u32 frameIndex)
			: graphResources(graphResources)
			, commandList(commandList)
			, globalSet(globalSet)
			, texturesSet(texturesSet)
			, textSet(textSet)
			, frameIndex(frameIndex)
		{
		}

		Renderer::RenderGraphResources& graphResources;
		Renderer::CommandList& commandList;

		Renderer::DescriptorSetResource& globalSet;
		Renderer::DescriptorSetResource& texturesSet;
		Renderer::DescriptorSetResource& textSet;

		u32 frameIndex;
	};
	void Draw(DrawParams& params);

	void _DrawLayer(DrawParams& params, Renderer::GraphicsPipelineID texturesPipeline, Renderer::GraphicsPipelineID textPipeline, CanvasLayer& layer, uvec2 renderSize);

	void _CreateLayer();
private:
	Renderer::Renderer& _renderer;

    Renderer::ImageID _renderTarget;
	Renderer::ImageMutableResource _renderTargetResource;

	std::vector<CanvasLayer> _layers;

    friend class CanvasRenderer;
};