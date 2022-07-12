#pragma once
#include <Base/Types.h>

#include <Renderer/DescriptorSet.h>
#include <Renderer/GPUVector.h>

struct RenderResources;

namespace Renderer
{
	class Renderer;
	class RenderGraph;
}

class CubeRenderer
{
public:
	CubeRenderer(Renderer::Renderer* renderer);
	~CubeRenderer();

	void Update(f32 deltaTime);

	void AddGeometryPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);

private:
	void CreatePermanentResources();

	struct Vertex
	{
		vec4 position;
		vec4 color;
	};

private:
	Renderer::Renderer* _renderer = nullptr;

	Renderer::SamplerID _linearSampler;

	Renderer::GPUVector<Vertex> _vertices;
	Renderer::GPUVector<u16> _indices;

	Renderer::DescriptorSet _geometryDescriptorSet;
};