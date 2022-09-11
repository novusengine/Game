#pragma once
#include <Base/Types.h>
#include <Renderer/DescriptorSet.h>

class DebugRenderer;
struct RenderResources;
class SimplexNoise;

namespace Renderer
{
	class Renderer;
	class RenderGraph;
}

class TerrainRenderer
{
public:

public:
	TerrainRenderer(Renderer::Renderer* renderer, DebugRenderer* debugRenderer);
	~TerrainRenderer();

	void Update(f32 deltaTime);
	void Clear();

	void AddCullingPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);
	void AddGeometryPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);
	
private:
	void CreatePermanentResources();

	void SyncToGPU();

private:
	Renderer::Renderer* _renderer = nullptr;
	DebugRenderer* _debugRenderer = nullptr;
};