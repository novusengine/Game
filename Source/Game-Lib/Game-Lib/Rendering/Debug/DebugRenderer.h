#pragma once
#include <Base/Types.h>

#include <Renderer/DescriptorSet.h>
#include <Renderer/GPUVector.h>

struct RenderResources;

namespace Renderer
{
	class Renderer;
	class RenderGraph;
    class RenderGraphBuilder;
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

	// Wireframe
	void DrawLine2D(const vec2& from, const vec2& to, Color color);
	void DrawLine3D(const vec3& from, const vec3& to, Color color);

	void DrawAABB3D(const vec3& center, const vec3& extents, Color color);
	void DrawOBB3D(const vec3& center, const vec3& extents, const quat& rotation, Color color);
	void DrawTriangle2D(const vec2& v0, const vec2& v1, const vec2& v2, Color color);
	void DrawTriangle3D(const vec3& v0, const vec3& v1, const vec3& v2, Color color);

	void DrawCircle3D(const vec3& center, f32 radius, i32 resolution, Color color);
	void DrawSphere3D(const vec3& center, f32 radius, i32 resolution, Color color);

	void DrawFrustum(const mat4x4& viewProjectionMatrix, Color color);
	void DrawMatrix(const mat4x4& matrix, f32 scale);

	// Solid
	void DrawLineSolid2D(const vec2& from, const vec2& to, Color color, bool shaded = false);

	void DrawAABBSolid3D(const vec3& center, const vec3& extents, Color color, bool shaded = false);
	void DrawOBBSolid3D(const vec3& center, const vec3& extents, const quat& rotation, Color color, bool shaded = false);
	void DrawTriangleSolid2D(const vec2& v0, const vec2& v1, const vec2& v2, Color color, bool shaded = false);
	void DrawTriangleSolid3D(const vec3& v0, const vec3& v1, const vec3& v2, Color color, bool shaded = false);

	static vec3 UnProject(const vec3& point, const mat4x4& m);

	Renderer::DescriptorSet& GetDebugDescriptorSet() { return _debugDescriptorSet; }
	void RegisterCullingPassBufferUsage(Renderer::RenderGraphBuilder& builder);

private:
	Renderer::Renderer* _renderer = nullptr;

	struct DebugVertex2D
	{
		vec2 pos;
		u32 color;
	};

	struct DebugVertex3D
	{
		vec3 pos;
		u32 color;
	};

	struct DebugVertexSolid3D
	{
		vec4 pos;
		vec4 normalAndColor; // xyz = normal, int(w) = color
	};

	Renderer::DescriptorSet _debugDescriptorSet;

	// Wireframe
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

	// Solid
	Renderer::GPUVector<DebugVertex2D> _debugVerticesSolid2D;
	Renderer::GPUVector<DebugVertexSolid3D> _debugVerticesSolid3D;

	Renderer::DescriptorSet _drawSolid2DDescriptorSet;
	Renderer::DescriptorSet _drawSolid3DDescriptorSet;

};
