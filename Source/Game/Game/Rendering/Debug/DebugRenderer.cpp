#include "DebugRenderer.h"
#include "Game/Rendering/RenderResources.h"

#include <Base/CVarSystem/CVarSystem.h>

#include <Renderer/Renderer.h>
#include <Renderer/RenderGraph.h>
#include <Renderer/Descriptors/ImageDesc.h>

AutoCVar_Int CVAR_DebugRendererNumGPUVertices("debugRenderer.numGPUVertices", "number of GPU vertices to allocate for", 32000000);

DebugRenderer::DebugRenderer(Renderer::Renderer* renderer)
{
	_renderer = renderer;

	_debugVertices2D.SetDebugName("DebugVertices2D");
	_debugVertices2D.SetUsage(Renderer::BufferUsage::TRANSFER_DESTINATION | Renderer::BufferUsage::STORAGE_BUFFER);
	_debugVertices2D.SyncToGPU(_renderer);
	_draw2DDescriptorSet.Bind("_vertices", _debugVertices2D.GetBuffer());

	_debugVertices3D.SetDebugName("DebugVertices3D");
	_debugVertices3D.SetUsage(Renderer::BufferUsage::TRANSFER_DESTINATION | Renderer::BufferUsage::STORAGE_BUFFER);
	_debugVertices3D.SyncToGPU(_renderer);
	_draw3DDescriptorSet.Bind("_vertices", _debugVertices3D.GetBuffer());

	// Create indirect buffers for GPU-side debugging
	u32 numGPUVertices = CVAR_DebugRendererNumGPUVertices.Get();
	{
		Renderer::BufferDesc desc;
		desc.size = sizeof(DebugVertex2D) * numGPUVertices;
		desc.usage = Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION;

		_gpuDebugVertices2D = _renderer->CreateBuffer(_gpuDebugVertices2D, desc);
		_draw2DIndirectDescriptorSet.Bind("_vertices", _gpuDebugVertices2D);
		_debugDescriptorSet.Bind("_debugVertices2D", _gpuDebugVertices2D);
	}

	{
		Renderer::BufferDesc desc;
		desc.size = sizeof(DebugVertex3D) * numGPUVertices;
		desc.usage = Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION;

		_gpuDebugVertices3D = _renderer->CreateBuffer(_gpuDebugVertices3D, desc);
		_draw3DIndirectDescriptorSet.Bind("_vertices", _gpuDebugVertices3D);
		_debugDescriptorSet.Bind("_debugVertices3D", _gpuDebugVertices3D);
	}

	// Create indirect argument buffers for GPU-side debugging
	{
		Renderer::BufferDesc desc;
		desc.size = sizeof(Renderer::IndirectDraw);
		desc.usage = Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION | Renderer::BufferUsage::INDIRECT_ARGUMENT_BUFFER;

		_gpuDebugVertices2DArgumentBuffer = _renderer->CreateAndFillBuffer(_gpuDebugVertices2DArgumentBuffer, desc, [](void* mappedMemory, size_t size)
		{
			Renderer::IndirectDraw* indirectDraw = static_cast<Renderer::IndirectDraw*>(mappedMemory);
			indirectDraw->instanceCount = 1;
		});
		_debugDescriptorSet.Bind("_debugVertices2DCount", _gpuDebugVertices2DArgumentBuffer);
	}

	{
		Renderer::BufferDesc desc;
		desc.size = sizeof(Renderer::IndirectDraw);
		desc.usage = Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION | Renderer::BufferUsage::INDIRECT_ARGUMENT_BUFFER;

		_gpuDebugVertices3DArgumentBuffer = _renderer->CreateAndFillBuffer(_gpuDebugVertices3DArgumentBuffer, desc, [](void* mappedMemory, size_t size)
		{
			Renderer::IndirectDraw* indirectDraw = static_cast<Renderer::IndirectDraw*>(mappedMemory);
			indirectDraw->instanceCount = 1;
		});
		_debugDescriptorSet.Bind("_debugVertices3DCount", _gpuDebugVertices3DArgumentBuffer);
	}
}

void DebugRenderer::Update(f32 deltaTime)
{
	// Draw world axises
	//DrawLine3D(vec3(0.0f, 0.0f, 0.0f), vec3(100.0f, 0.0f, 0.0f), 0xff0000ff);
	//DrawLine3D(vec3(0.0f, 0.0f, 0.0f), vec3(0.0f, 100.0f, 0.0f), 0xff00ff00);
	//DrawLine3D(vec3(0.0f, 0.0f, 0.0f), vec3(0.0f, 0.0f, 100.0f), 0xffff0000);

	// Sync to GPU
	if (_debugVertices2D.SyncToGPU(_renderer))
	{
		_draw2DDescriptorSet.Bind("_vertices", _debugVertices2D.GetBuffer());
	}
	if (_debugVertices3D.SyncToGPU(_renderer))
	{
		_draw3DDescriptorSet.Bind("_vertices", _debugVertices3D.GetBuffer());
	}
}

void DebugRenderer::AddStartFramePass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex)
{
	struct Data
	{
	};

	renderGraph->AddPass<Data>("DebugRenderReset",
	[=, &resources](Data& data, Renderer::RenderGraphBuilder& builder) // Setup
		{
			return true;// Return true from setup to enable this pass, return false to disable it
		},
		[=, &resources](Data& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList) // Execute
		{
			GPU_SCOPED_PROFILER_ZONE(commandList, DebugRenderReset);

			commandList.FillBuffer(_gpuDebugVertices2DArgumentBuffer, 0, 4, 0); // Reset vertexCount to 0
			commandList.FillBuffer(_gpuDebugVertices3DArgumentBuffer, 0, 4, 0); // Reset vertexCount to 0
			commandList.PipelineBarrier(Renderer::PipelineBarrierType::TransferDestToComputeShaderRW, _gpuDebugVertices2DArgumentBuffer);
			commandList.PipelineBarrier(Renderer::PipelineBarrierType::TransferDestToComputeShaderRW, _gpuDebugVertices3DArgumentBuffer);
		});
}

void DebugRenderer::Add2DPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex)
{
	struct Data
	{
		Renderer::RenderPassMutableResource color;
	};
	renderGraph->AddPass<Data>("DebugRender2D",
		[=, &resources](Data& data, Renderer::RenderGraphBuilder& builder) // Setup
		{
			data.color = builder.Write(resources.finalColor, Renderer::RenderGraphBuilder::WriteMode::RENDERTARGET, Renderer::RenderGraphBuilder::LoadMode::LOAD);

			return true;// Return true from setup to enable this pass, return false to disable it
		},
		[=, &resources](Data& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList) // Execute
		{
			GPU_SCOPED_PROFILER_ZONE(commandList, DebugRender2D);

			Renderer::GraphicsPipelineDesc pipelineDesc;
			graphResources.InitializePipelineDesc(pipelineDesc);

			// Rasterizer state
			pipelineDesc.states.rasterizerState.cullMode = Renderer::CullMode::BACK;

			// Render targets.
			pipelineDesc.renderTargets[0] = data.color;

			// Shader
			Renderer::VertexShaderDesc vertexShaderDesc;
			vertexShaderDesc.path = "debug2D.vs.hlsl";

			Renderer::PixelShaderDesc pixelShaderDesc;
			pixelShaderDesc.path = "debug2D.ps.hlsl";

			pipelineDesc.states.vertexShader = _renderer->LoadShader(vertexShaderDesc);
			pipelineDesc.states.pixelShader = _renderer->LoadShader(pixelShaderDesc);

			pipelineDesc.states.primitiveTopology = Renderer::PrimitiveTopology::Lines;

			Renderer::GraphicsPipelineID pipeline = _renderer->CreatePipeline(pipelineDesc);

			// CPU side debug rendering
			{
				commandList.BeginPipeline(pipeline);

				commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::GLOBAL, &resources.globalDescriptorSet, frameIndex);
				commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS, &_draw2DDescriptorSet, frameIndex);

				// Draw
				commandList.Draw(static_cast<u32>(_debugVertices2D.Size()), 1, 0, 0);

				commandList.EndPipeline(pipeline);
			}
			_debugVertices2D.Clear(false);

			commandList.ImageBarrier(resources.finalColor);

			// GPU side debug rendering
			commandList.PipelineBarrier(Renderer::PipelineBarrierType::ComputeWriteToComputeShaderRead, _gpuDebugVertices2D);
			commandList.PipelineBarrier(Renderer::PipelineBarrierType::ComputeWriteToIndirectArguments, _gpuDebugVertices2DArgumentBuffer);
			{
				commandList.BeginPipeline(pipeline);

				commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::GLOBAL, &resources.globalDescriptorSet, frameIndex);
				commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS, &_draw2DIndirectDescriptorSet, frameIndex);

				// Draw
				commandList.DrawIndirect(_gpuDebugVertices2DArgumentBuffer, 0, 1);

				commandList.EndPipeline(pipeline);
			}
		});
}

void DebugRenderer::Add3DPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex)
{
	struct Data
	{
		Renderer::RenderPassMutableResource color;
		Renderer::RenderPassMutableResource depth;
	};
	renderGraph->AddPass<Data>("DebugRender3D",
		[=, &resources](Data& data, Renderer::RenderGraphBuilder& builder) // Setup
		{
			data.color = builder.Write(resources.finalColor, Renderer::RenderGraphBuilder::WriteMode::RENDERTARGET, Renderer::RenderGraphBuilder::LoadMode::LOAD);
			data.depth = builder.Write(resources.depth, Renderer::RenderGraphBuilder::WriteMode::RENDERTARGET, Renderer::RenderGraphBuilder::LoadMode::LOAD);

			return true;// Return true from setup to enable this pass, return false to disable it
		},
		[=, &resources](Data& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList) // Execute
		{
			GPU_SCOPED_PROFILER_ZONE(commandList, DebugRender3D);

			Renderer::GraphicsPipelineDesc pipelineDesc;
			graphResources.InitializePipelineDesc(pipelineDesc);

			// Shader
			Renderer::VertexShaderDesc vertexShaderDesc;
			vertexShaderDesc.path = "debug3D.vs.hlsl";

			Renderer::PixelShaderDesc pixelShaderDesc;
			pixelShaderDesc.path = "debug3D.ps.hlsl";

			pipelineDesc.states.vertexShader = _renderer->LoadShader(vertexShaderDesc);
			pipelineDesc.states.pixelShader = _renderer->LoadShader(pixelShaderDesc);

			pipelineDesc.states.primitiveTopology = Renderer::PrimitiveTopology::Lines;

			// Depth state
			pipelineDesc.states.depthStencilState.depthEnable = true;
			pipelineDesc.states.depthStencilState.depthWriteEnable = false;
			pipelineDesc.states.depthStencilState.depthFunc = Renderer::ComparisonFunc::GREATER;

			// Rasterizer state
			pipelineDesc.states.rasterizerState.cullMode = Renderer::CullMode::BACK;
			pipelineDesc.states.rasterizerState.frontFaceMode = Renderer::FrontFaceState::COUNTERCLOCKWISE;

			pipelineDesc.renderTargets[0] = data.color;

			pipelineDesc.depthStencil = data.depth;

			// Set pipeline
			Renderer::GraphicsPipelineID pipeline = _renderer->CreatePipeline(pipelineDesc);

			// CPU side debug rendering
			{
				commandList.BeginPipeline(pipeline);

				commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::GLOBAL, &resources.globalDescriptorSet, frameIndex);
				commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS, &_draw3DDescriptorSet, frameIndex);

				// Draw
				commandList.Draw(static_cast<u32>(_debugVertices3D.Size()), 1, 0, 0);

				commandList.EndPipeline(pipeline);
			}
			_debugVertices3D.Clear(false);

			commandList.ImageBarrier(resources.finalColor);
			
			// GPU side debug rendering
			commandList.PipelineBarrier(Renderer::PipelineBarrierType::ComputeWriteToComputeShaderRead, _gpuDebugVertices3D);
			commandList.PipelineBarrier(Renderer::PipelineBarrierType::ComputeWriteToVertexShaderRead, _gpuDebugVertices3D);
			commandList.PipelineBarrier(Renderer::PipelineBarrierType::ComputeWriteToIndirectArguments, _gpuDebugVertices3DArgumentBuffer);
			{
				commandList.BeginPipeline(pipeline);

				commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::GLOBAL, &resources.globalDescriptorSet, frameIndex);
				commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS, &_draw3DIndirectDescriptorSet, frameIndex);

				// Draw
				commandList.DrawIndirect(_gpuDebugVertices3DArgumentBuffer, 0, 1);

				commandList.EndPipeline(pipeline);
			}
		});
}

void DebugRenderer::DrawLine2D(const glm::vec2& from, const glm::vec2& to, uint32_t color)
{
	auto& vertices = _debugVertices2D.Get();

	vertices.push_back({ from, color });
	vertices.push_back({ to, color });
}

void DebugRenderer::DrawLine3D(const glm::vec3& from, const glm::vec3& to, uint32_t color)
{
	auto& vertices = _debugVertices3D.Get();

	vertices.push_back({ from, color });
	vertices.push_back({ to, color });
}

void DebugRenderer::DrawAABB3D(const vec3& center, const vec3& extents, uint32_t color)
{
	auto& vertices = _debugVertices3D.Get();

	vec3 v0 = center - extents;
	vec3 v1 = center + extents;

	// Bottom
	vertices.push_back({ { v0.x, v0.y, v0.z }, color });
	vertices.push_back({ { v1.x, v0.y, v0.z }, color });
	vertices.push_back({ { v1.x, v0.y, v0.z }, color });
	vertices.push_back({ { v1.x, v0.y, v1.z }, color });
	vertices.push_back({ { v1.x, v0.y, v1.z }, color });
	vertices.push_back({ { v0.x, v0.y, v1.z }, color });
	vertices.push_back({ { v0.x, v0.y, v1.z }, color });
	vertices.push_back({ { v0.x, v0.y, v0.z }, color });

	// Top
	vertices.push_back({ { v0.x, v1.y, v0.z }, color });
	vertices.push_back({ { v1.x, v1.y, v0.z }, color });
	vertices.push_back({ { v1.x, v1.y, v0.z }, color });
	vertices.push_back({ { v1.x, v1.y, v1.z }, color });
	vertices.push_back({ { v1.x, v1.y, v1.z }, color });
	vertices.push_back({ { v0.x, v1.y, v1.z }, color });
	vertices.push_back({ { v0.x, v1.y, v1.z }, color });
	vertices.push_back({ { v0.x, v1.y, v0.z }, color });

	// Vertical edges
	vertices.push_back({ { v0.x, v0.y, v0.z }, color });
	vertices.push_back({ { v0.x, v1.y, v0.z }, color });
	vertices.push_back({ { v1.x, v0.y, v0.z }, color });
	vertices.push_back({ { v1.x, v1.y, v0.z }, color });
	vertices.push_back({ { v0.x, v0.y, v1.z }, color });
	vertices.push_back({ { v0.x, v1.y, v1.z }, color });
	vertices.push_back({ { v1.x, v0.y, v1.z }, color });
	vertices.push_back({ { v1.x, v1.y, v1.z }, color });
}

void DebugRenderer::DrawTriangle2D(const glm::vec2& v0, const glm::vec2& v1, const glm::vec2& v2, uint32_t color)
{
	DrawLine2D(v0, v1, color);
	DrawLine2D(v1, v2, color);
	DrawLine2D(v2, v0, color);
}

void DebugRenderer::DrawTriangle3D(const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2, uint32_t color)
{
	DrawLine3D(v0, v1, color);
	DrawLine3D(v1, v2, color);
	DrawLine3D(v2, v0, color);
}

void DebugRenderer::DrawCircle3D(const vec3& center, f32 radius, i32 resolution, uint32_t color)
{
	auto& vertices = _debugVertices3D.Get();

	constexpr f32 PI = glm::pi<f32>();
	constexpr f32 TAU = PI * 2.0f;

	f32 increment = TAU / resolution;
	for (f32 currentAngle = 0.0f; currentAngle <= TAU; currentAngle += increment)
	{
		vec3 pos = vec3(radius * glm::cos(currentAngle) + center.x, radius * glm::sin(currentAngle) + center.y, center.z);
		vertices.push_back({ { pos.x, pos.y, pos.z }, color });
	}
}

vec3 DebugRenderer::UnProject(const vec3& point, const mat4x4& m)
{
	vec4 obj = m * vec4(point, 1.0f);
	obj /= obj.w;
	return vec3(obj);
}

void DebugRenderer::DrawFrustum(const mat4x4& viewProjectionMatrix, uint32_t color)
{
	const mat4x4 m = glm::inverse(viewProjectionMatrix);

	const vec3 near0 = UnProject(vec3(-1.0f, -1.0f, 0.0f), m);
	const vec3 near1 = UnProject(vec3(+1.0f, -1.0f, 0.0f), m);
	const vec3 near2 = UnProject(vec3(+1.0f, +1.0f, 0.0f), m);
	const vec3 near3 = UnProject(vec3(-1.0f, +1.0f, 0.0f), m);

	const vec3 far0 = UnProject(vec3(-1.0f, -1.0f, 1.0f), m);
	const vec3 far1 = UnProject(vec3(+1.0f, -1.0f, 1.0f), m);
	const vec3 far2 = UnProject(vec3(+1.0f, +1.0f, 1.0f), m);
	const vec3 far3 = UnProject(vec3(-1.0f, +1.0f, 1.0f), m);

	// Near plane
	DrawLine3D(near0, near1, color);
	DrawLine3D(near1, near2, color);
	DrawLine3D(near2, near3, color);
	DrawLine3D(near3, near0, color);

	// Far plane
	DrawLine3D(far0, far1, color);
	DrawLine3D(far1, far2, color);
	DrawLine3D(far2, far3, color);
	DrawLine3D(far3, far0, color);

	// Edges
	DrawLine3D(near0, far0, color);
	DrawLine3D(near1, far1, color);
	DrawLine3D(near2, far2, color);
	DrawLine3D(near3, far3, color);
}

void DebugRenderer::DrawMatrix(const mat4x4& matrix, f32 scale)
{
	const vec3 origin = vec3(matrix[3].x, matrix[3].y, matrix[3].z);

	DrawLine3D(origin, origin + (vec3(matrix[0].x, matrix[0].y, matrix[0].z) * scale), 0xff0000ff);
	DrawLine3D(origin, origin + (vec3(matrix[1].x, matrix[1].y, matrix[1].z) * scale), 0xff00ff00);
	DrawLine3D(origin, origin + (vec3(matrix[2].x, matrix[2].y, matrix[2].z) * scale), 0xffff0000);
}