#include "Canvas.h"

#include <Renderer/Renderer.h>
#include <Renderer/Font.h>
#include <Renderer/CommandList.h>
#include <Renderer/RenderGraphResources.h>
#include <Renderer/RenderGraphBuilder.h>

#include <utfcpp/utf8.h>
#include <tracy/Tracy.hpp>

Canvas::Canvas(Renderer::Renderer* renderer, Renderer::ImageID renderTarget)
	: _renderer(*renderer)
	, _renderTarget(renderTarget)
{
	_CreateLayer();
}

void CalculateVertices(vec2 pos, vec2 size, uvec2 renderTargetSize, std::vector<CanvasTextVertex>& vertices)
{
	vec2 upperLeftPos = vec2(pos.x, pos.y);
	vec2 upperRightPos = vec2(pos.x + size.x, pos.y);
	vec2 lowerLeftPos = vec2(pos.x, pos.y + size.y);
	vec2 lowerRightPos = vec2(pos.x + size.x, pos.y + size.y);

	// UV space
	upperLeftPos /= renderTargetSize;
	upperRightPos /= renderTargetSize;
	lowerLeftPos /= renderTargetSize;
	lowerRightPos /= renderTargetSize;

	// Vertices
	CanvasTextVertex upperLeft;
	upperLeft.posAndUV = vec4(upperLeftPos, 0, 1);

	CanvasTextVertex upperRight;
	upperRight.posAndUV = vec4(upperRightPos, 1, 1);

	CanvasTextVertex lowerLeft;
	lowerLeft.posAndUV = vec4(lowerLeftPos, 0, 0);

	CanvasTextVertex lowerRight;
	lowerRight.posAndUV = vec4(lowerRightPos, 1, 0);

	vertices.push_back(upperLeft);
	vertices.push_back(upperRight);
	vertices.push_back(lowerLeft);

	vertices.push_back(upperRight);
	vertices.push_back(lowerRight);
	vertices.push_back(lowerLeft);
}

void Canvas::DrawText(Renderer::Font& font, const std::string& text, const vec2& textPos, i8 layer)
{
	ZoneScoped;
	// TODO: Multithreading issue when several threads are drawing to the same layer

	// Layer -1 means the topmost layer
	if (layer == -1)
	{
		layer = static_cast<i8>(_layers.size() - 1);
	}

	// Create layer if necessary
	while (layer >= _layers.size())
	{
		_CreateLayer();
		layer = static_cast<i8>(_layers.size() - 1);
	}

	CanvasLayer& canvasLayer = _layers[layer];
	uvec2 renderTargetSize = _renderer.GetImageDimensions(_renderTarget);

	std::vector<CanvasTextVertex>& textVertices = canvasLayer.textVertices.Get();
	u32 vertexOffset = static_cast<u32>(textVertices.size());
	textVertices.reserve(vertexOffset + text.size() * 6);
	
	std::vector<CanvasCharDrawData>& charDrawDatas = canvasLayer.charDrawData.Get();
	charDrawDatas.reserve(vertexOffset + text.size());

	utf8::iterator it(text.begin(), text.begin(), text.end());
	utf8::iterator endit(text.end(), text.begin(), text.end());

	vec2 currentPos = textPos;
	for (; it != endit; ++it)
	{
		unsigned int c = *it;

		if (c == ' ')
		{
			currentPos.x += 10;
			continue;
		}

		Renderer::FontChar& fontChar = font.GetChar(c);

		vec2 pos = currentPos - vec2(-fontChar.xOffset, fontChar.yOffset + fontChar.height);
		vec2 size = vec2(fontChar.width, fontChar.height);

		// Add vertices
		CalculateVertices(pos, size, renderTargetSize, textVertices);

		// Add char draw data
		CanvasCharDrawData& charDrawData = charDrawDatas.emplace_back();
		charDrawData.data.x = fontChar.textureIndex;
		charDrawData.data.y = Color::White.ToABGR32();
		charDrawData.data.z = Color::Black.ToABGR32();

		f32 outlineWidth = 0.5f;
		charDrawData.data.w = *reinterpret_cast<i32*>(&outlineWidth);

		currentPos.x += fontChar.advance;
	}
}

void Canvas::DrawTexture(CanvasTextureID textureID, const vec2& pos, const vec2& size, const uvec4& slicingOffsetPixels, i8 layer)
{
	ZoneScoped;
	// TODO: Multithreading issue when several threads are drawing to the same layer

	// Layer -1 means the topmost layer
	if (layer == -1)
	{
		layer = static_cast<i8>(_layers.size() - 1);
	}

	// Create layer if necessary
	while (layer >= _layers.size())
	{
		_CreateLayer();
		layer = static_cast<i8>(_layers.size() - 1);
	}

	CanvasLayer& canvasLayer = _layers[layer];
	std::vector<CanvasTextureDrawData>& textureDrawDatas = canvasLayer.textureDrawData.Get();

	CanvasTextureDrawData& textureDrawData = textureDrawDatas.emplace_back();
	textureDrawData.textureIndex = ivec4(static_cast<CanvasTextureID::type>(textureID), 0, 0, 0);

	uvec4 posAndSize = uvec4(pos, size); // Pixel space
	textureDrawData.positionAndSize = posAndSize;

	textureDrawData.slicingOffset = slicingOffsetPixels;
}

void Canvas::Setup(Renderer::RenderGraphBuilder& builder)
{
	_renderTargetResource = builder.Write(_renderTarget, Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::LOAD);

	for (CanvasLayer& layer : _layers)
	{
		layer.textureDrawData.SyncToGPU(&_renderer);
		layer.charDrawData.SyncToGPU(&_renderer);
		layer.textVertices.SyncToGPU(&_renderer);

		layer.textureDrawDataResource = builder.Read(layer.textureDrawData.GetBuffer(), Renderer::BufferPassUsage::GRAPHICS);
		layer.textVerticesResource = builder.Read(layer.textVertices.GetBuffer(), Renderer::BufferPassUsage::GRAPHICS);
		layer.charDrawDataResource = builder.Read(layer.charDrawData.GetBuffer(), Renderer::BufferPassUsage::GRAPHICS);
	}
}

void Canvas::_DrawLayer(DrawParams& params, Renderer::GraphicsPipelineID texturesPipeline, Renderer::GraphicsPipelineID textPipeline, CanvasLayer& layer, uvec2 renderSize)
{
	// Textures
	if (layer.textureDrawData.Size() > 0)
	{
		params.texturesSet.Bind("_drawData", layer.textureDrawDataResource);

		params.commandList.BeginPipeline(texturesPipeline);

		params.commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::GLOBAL, params.globalSet, params.frameIndex);
		params.commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS, params.texturesSet, params.frameIndex);

		struct Constants
		{
			uvec4 renderSize;
		};
		Constants* constants = params.graphResources.FrameNew<Constants>();
		constants->renderSize = uvec4(renderSize, 0, 0);

		params.commandList.PushConstant(constants, 0, sizeof(Constants));

		params.commandList.Draw(6 * static_cast<u32>(layer.textureDrawData.Size()), 1, 0, 0);

		params.commandList.EndPipeline(texturesPipeline);
		layer.textureDrawData.Clear();
	}
	
	// Text
	if (layer.charDrawData.Size() > 0)
	{
		params.textSet.Bind("_charData", layer.charDrawDataResource);
		params.textSet.Bind("_vertices", layer.textVerticesResource);

		params.commandList.BeginPipeline(textPipeline);

		params.commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::GLOBAL, params.globalSet, params.frameIndex);
		params.commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS, params.textSet, params.frameIndex);

		params.commandList.Draw(6 * static_cast<u32>(layer.charDrawData.Size()), 1, 0, 0);

		params.commandList.EndPipeline(textPipeline);
		layer.charDrawData.Clear();
		layer.textVertices.Clear();
	}
}

void Canvas::_CreateLayer()
{
	u32 layerIndex = static_cast<u32>(_layers.size());
	CanvasLayer& layer = _layers.emplace_back();

	layer.textureDrawData.SetDebugName("TextureDrawData");
	layer.textureDrawData.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);

	layer.charDrawData.SetDebugName("CharDrawData");
	layer.charDrawData.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);

	layer.textVertices.SetDebugName("TextVertices");
	layer.textVertices.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);
}

void Canvas::Draw(DrawParams& params)
{
	// Create pipelines
	Renderer::GraphicsPipelineID texturesPipeline = Renderer::GraphicsPipelineID::Invalid();
	{
		Renderer::GraphicsPipelineDesc pipelineDesc;
		params.graphResources.InitializePipelineDesc(pipelineDesc);

		// Rasterizer state
		pipelineDesc.states.rasterizerState.frontFaceMode = Renderer::Settings::FRONT_FACE_STATE;
		pipelineDesc.states.rasterizerState.cullMode = Renderer::CullMode::BACK;

		// Render targets.
		pipelineDesc.renderTargets[0] = _renderTargetResource;

		// Shader
		Renderer::VertexShaderDesc vertexShaderDesc;
		vertexShaderDesc.path = "Canvas/Texture.vs.hlsl";

		Renderer::PixelShaderDesc pixelShaderDesc;
		pixelShaderDesc.path = "Canvas/Texture.ps.hlsl";

		pipelineDesc.states.vertexShader = _renderer.LoadShader(vertexShaderDesc);
		pipelineDesc.states.pixelShader = _renderer.LoadShader(pixelShaderDesc);

		texturesPipeline = _renderer.CreatePipeline(pipelineDesc);
	}

	Renderer::GraphicsPipelineID textPipeline = Renderer::GraphicsPipelineID::Invalid();
	{
		Renderer::GraphicsPipelineDesc pipelineDesc;
		params.graphResources.InitializePipelineDesc(pipelineDesc);

		// Rasterizer state
		pipelineDesc.states.rasterizerState.frontFaceMode = Renderer::Settings::FRONT_FACE_STATE;
		pipelineDesc.states.rasterizerState.cullMode = Renderer::CullMode::BACK;

		// Render targets.
		pipelineDesc.renderTargets[0] = _renderTargetResource;

		// Shader
		Renderer::VertexShaderDesc vertexShaderDesc;
		vertexShaderDesc.path = "Canvas/Text.vs.hlsl";

		Renderer::PixelShaderDesc pixelShaderDesc;
		pixelShaderDesc.path = "Canvas/Text.ps.hlsl";

		pipelineDesc.states.vertexShader = _renderer.LoadShader(vertexShaderDesc);
		pipelineDesc.states.pixelShader = _renderer.LoadShader(pixelShaderDesc);

		// Blending
		pipelineDesc.states.blendState.renderTargets[0].blendEnable = true;
		pipelineDesc.states.blendState.renderTargets[0].srcBlend = Renderer::BlendMode::SRC_ALPHA;
		pipelineDesc.states.blendState.renderTargets[0].destBlend = Renderer::BlendMode::INV_SRC_ALPHA;
		pipelineDesc.states.blendState.renderTargets[0].srcBlendAlpha = Renderer::BlendMode::ZERO;
		pipelineDesc.states.blendState.renderTargets[0].destBlendAlpha = Renderer::BlendMode::ONE;

		textPipeline = _renderer.CreatePipeline(pipelineDesc);
	}

	uvec2 renderSize = params.graphResources.GetImageDimensions(_renderTargetResource);

	// Draw layers
	for (CanvasLayer& layer : _layers)
	{
		_DrawLayer(params, texturesPipeline, textPipeline, layer, renderSize);
	}
}