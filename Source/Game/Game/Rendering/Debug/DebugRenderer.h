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

class DebugRenderer
{
public:
	DebugRenderer(Renderer::Renderer* renderer);
	~DebugRenderer();

	void Update(f32 deltaTime);

	void AddStartFramePass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);
	void Add2DPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);
	void Add3DPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);

	void DrawLine2D(const vec2& from, const vec2& to, uint32_t color);
	void DrawLine3D(const vec3& from, const vec3& to, uint32_t color);

	void DrawAABB3D(const vec3& center, const vec3& extents, uint32_t color);
	void DrawTriangle2D(const vec2& v0, const vec2& v1, const vec2& v2, uint32_t color);
	void DrawTriangle3D(const vec3& v0, const vec3& v1, const vec3& v2, uint32_t color);

	void DrawCircle3D(const vec3& center, f32 radius, i32 resolution, uint32_t color);

	void DrawFrustum(const mat4x4& viewProjectionMatrix, uint32_t color);
	void DrawMatrix(const mat4x4& matrix, f32 scale);

	static vec3 UnProject(const vec3& point, const mat4x4& m);

	Renderer::DescriptorSet& GetDebugDescriptorSet() { return _debugDescriptorSet; }

private:

private:
	Renderer::Renderer* _renderer = nullptr;

	struct DebugVertex2D
	{
		glm::vec2 pos;
		uint32_t color;
	};

	struct DebugVertex3D
	{
		glm::vec3 pos;
		uint32_t color;
	};

	Renderer::GPUVector<DebugVertex2D> _debugVertices2D;
	Renderer::GPUVector<DebugVertex3D> _debugVertices3D;

	Renderer::BufferID _gpuDebugVertices2D;
	Renderer::BufferID _gpuDebugVertices2DArgumentBuffer;
	Renderer::BufferID _gpuDebugVertices3D;
	Renderer::BufferID _gpuDebugVertices3DArgumentBuffer;

	Renderer::DescriptorSet _draw2DDescriptorSet;
	Renderer::DescriptorSet _draw2DIndirectDescriptorSet;
	Renderer::DescriptorSet _draw3DDescriptorSet;
	Renderer::DescriptorSet _draw3DIndirectDescriptorSet;
	Renderer::DescriptorSet _debugDescriptorSet;
};