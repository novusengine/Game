#pragma once
#include <Game/Rendering/CulledRenderer.h>

#include <Base/Types.h>

namespace Renderer
{
	class Renderer;
}

class DebugRenderer;

class WaterRenderer : CulledRenderer
{
public:
	struct ReserveInfo
	{
		u32 numInstances = 0;
		u32 numVertices = 0;
		u32 numIndices = 0;
	};

private:
#pragma pack(push, 1)
	struct Vertex
	{
		u8 xCellOffset = 0;
		u8 yCellOffset = 0;
		f16 height = f16(0);
		hvec2 uv = hvec2(f16(0), f16(0));
	};

	struct DrawCallData
	{
		u16 chunkID;
		u16 cellID;
		u16 textureStartIndex;
		u8 textureCount;
		u8 hasDepth;
		u16 liquidType;
		u16 padding0;
		hvec2 uvAnim = hvec2(f16(1), f16(0)); // x seems to be scrolling, y seems to be rotation
	};
#pragma pack(pop)

public:
	WaterRenderer(Renderer::Renderer* renderer, DebugRenderer* debugRenderer);
	~WaterRenderer();

	void Update(f32 deltaTime);
	void Clear();

	void Reserve(ReserveInfo& info);
	void FitAfterGrow();

	struct LoadDesc
	{
		u32 chunkID;
		u32 cellID;
		u8 typeID;

		u8 posX;
		u8 posY;

		u8 width;
		u8 height;

		vec2 cellPos;

		u8 liquidOffsetX;
		u8 liquidOffsetY;

		u32 bitmapDataOffset;

		f32* heightMap = nullptr;
		u8* bitMap = nullptr;
	};
	void Load(LoadDesc& desc);

	void AddCopyDepthPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);
	void AddCullingPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);
	void AddGeometryPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);

private:
	void CreatePermanentResources();

	void SyncToGPU();

	void Draw(const RenderResources& resources, u8 frameIndex, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList, const DrawParams& params);

private:
	Renderer::Renderer* _renderer = nullptr;
	DebugRenderer* _debugRenderer = nullptr;

	Renderer::DescriptorSet _copyDescriptorSet;
	//Renderer::DescriptorSet _cullingDescriptorSet;
	//Renderer::DescriptorSet _passDescriptorSet;

	Renderer::SamplerID _sampler;

	CullingResources<DrawCallData> _cullingResources;
	std::atomic<u32> _instanceIndex = 0;

	Renderer::GPUVector<Vertex> _vertices;
	std::atomic<u32> _verticesIndex = 0;

	Renderer::GPUVector<u16> _indices;
	std::atomic<u32> _indicesIndex = 0;
};