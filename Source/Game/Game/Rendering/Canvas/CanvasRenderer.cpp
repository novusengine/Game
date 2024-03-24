#include "CanvasRenderer.h"

#include <Game/Rendering/RenderResources.h>

#include <Renderer/Renderer.h>
#include <Renderer/GPUVector.h>
#include <Renderer/RenderGraph.h>
#include <Renderer/Font.h>

struct Data
{
	std::shared_mutex canvasMutex;
	std::vector<Canvas> canvases;

	Renderer::TextureArrayID textures;

	std::shared_mutex textureDrawMutex;
	Renderer::GPUVector<CanvasTextureDrawData> textureDrawData;

	std::shared_mutex textDrawMutex;
	Renderer::GPUVector<CanvasTextVertex> textVertices;
	Renderer::GPUVector<CanvasCharDrawData> charDrawData;

	Renderer::Font* font = nullptr;
};

CanvasRenderer::CanvasRenderer(Renderer::Renderer* renderer)
	: _renderer(renderer)
	, _data(*new Data())
{
	CreatePermanentResources();

	// Usage examples
	//_data.debugTexture = _renderer->LoadTexture(textureDesc);
	//AddTexture(_data.debugTexture);
	//_data.mainCanvas = &CreateCanvas(Renderer::ImageID(3));
}

void CanvasRenderer::Update(f32 deltaTime)
{
	ZoneScoped;

	// Layer test and usage example
	//_data.mainCanvas->DrawText(*_data.font, u8"Jalapeño", vec2(100, 100));
	//_data.mainCanvas->DrawTexture(CanvasTextureID(0), vec2(100, 500), vec2(256, 128), uvec4(3, 3, 3, 3), 1);
}

CanvasTextureID CanvasRenderer::AddTexture(Renderer::TextureID textureID)
{
	u32 textureIndex = _renderer->AddTextureToArray(textureID, _data.textures);
	return CanvasTextureID(textureIndex);
}

Canvas& CanvasRenderer::CreateCanvas(Renderer::ImageID renderTarget)
{
	std::unique_lock lock(_data.canvasMutex);
	Canvas& canvas = _data.canvases.emplace_back(_renderer, renderTarget);

	return canvas;
}

void CanvasRenderer::AddCanvasPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex)
{
	struct Data
	{
		Renderer::DescriptorSetResource globalDescriptorSet;
		Renderer::DescriptorSetResource textureDescriptorSet;
		Renderer::DescriptorSetResource textDescriptorSet;
	};
	renderGraph->AddPass<Data>("Canvases",
		[this, &resources](Data& data, Renderer::RenderGraphBuilder& builder) // Setup
		{
			using BufferUsage = Renderer::BufferPassUsage;

			for (Canvas& canvas : _data.canvases)
			{
				canvas.Setup(builder);
			}

			data.globalDescriptorSet = builder.Use(resources.globalDescriptorSet);
			data.textureDescriptorSet = builder.Use(_textureDescriptorSet);
			data.textDescriptorSet = builder.Use(_textDescriptorSet);

			return true;// Return true from setup to enable this pass, return false to disable it
		},
		[this, &resources, frameIndex](Data& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList) // Execute
		{
			GPU_SCOPED_PROFILER_ZONE(commandList, DebugRender2D);

			Canvas::DrawParams params(graphResources, commandList, data.globalDescriptorSet, data.textureDescriptorSet, data.textDescriptorSet, frameIndex);
			for (Canvas& canvas : _data.canvases)
			{
				canvas.Draw(params);
			}
		});
}

void CanvasRenderer::CreatePermanentResources()
{
	Renderer::TextureArrayDesc textureArrayDesc;
	textureArrayDesc.size = 4096;

	_data.textures = _renderer->CreateTextureArray(textureArrayDesc);
	_textureDescriptorSet.Bind("_textures", _data.textures);

	_data.textureDrawData.SetDebugName("TextureDrawData");
	_data.textureDrawData.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);

	// Samplers
	Renderer::SamplerDesc samplerDesc;
	samplerDesc.enabled = true;
	samplerDesc.filter = Renderer::SamplerFilter::MIN_MAG_MIP_LINEAR;
	samplerDesc.addressU = Renderer::TextureAddressMode::CLAMP;
	samplerDesc.addressV = Renderer::TextureAddressMode::CLAMP;
	samplerDesc.addressW = Renderer::TextureAddressMode::CLAMP;
	samplerDesc.shaderVisibility = Renderer::ShaderVisibility::PIXEL;

	_sampler = _renderer->CreateSampler(samplerDesc);
	_textDescriptorSet.Bind("_sampler"_h, _sampler);
	_textureDescriptorSet.Bind("_sampler"_h, _sampler);

	_data.font = Renderer::Font::GetDefaultFont(_renderer, 64.0f);
	_textDescriptorSet.Bind("_textures"_h, _data.font->GetTextureArray());

	_data.charDrawData.SetDebugName("CharDrawData");
	_data.charDrawData.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);

	_data.textVertices.SetDebugName("TextVertices");
	_data.textVertices.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);
}
