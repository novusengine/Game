#pragma once
#include "TerrainUtils.h"

#include <Base/Types.h>
#include <Renderer/DescriptorSet.h>


struct RenderResources;

namespace Renderer
{
	class Renderer;
	class RenderGraph;
}

class TerrainRenderer
{
public:
	TerrainRenderer(Renderer::Renderer* renderer);
	~TerrainRenderer();

	void Update(f32 deltaTime);

	void AddTriangularizationPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);
	void AddGeometryPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);

private:
	void CreatePermanentResources();

	struct Vertex
	{
		vec4 position;
		vec4 normal;
	};

private:
	Renderer::Renderer* _renderer = nullptr;

	Chunk chunk;

	Renderer::SamplerID _linearSampler;

	Renderer::GPUVector<i32> _triangleConnectionTable;
	Renderer::GPUVector<i32> _cubeEdgeFlags;
	Renderer::GPUVector<f32> _voxels;

	// GPU workbuffers
	Renderer::BufferID _arguments;
	Renderer::BufferID _vertices;

	Renderer::DescriptorSet _marchingCubeDescriptorSet;
	Renderer::DescriptorSet _geometryDescriptorSet;
};