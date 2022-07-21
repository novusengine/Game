#include "ModelRenderer.h"
#include "Game/Rendering/RenderResources.h"

#include <Base/Memory/Bytebuffer.h>

#include <Renderer/RenderGraph.h>

ModelRenderer::ModelRenderer(Renderer::Renderer* renderer) : _renderer(renderer)
{
	CreatePermanentResources();
}

void ModelRenderer::CreatePermanentResources()
{
	_vertexPositions.SetDebugName("ModelVertexPositions");
	_vertexPositions.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);

	_vertexNormals.SetDebugName("ModelVertexNormals");
	_vertexNormals.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);

	_vertexUVs.SetDebugName("ModelVertexUVs");
	_vertexUVs.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);

	_indices.SetDebugName("ModelIndices");
	_indices.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);

	_meshlets.SetDebugName("ModelMeshlets");
	_meshlets.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);

	_instanceData.SetDebugName("ModelInstanceData");
	_instanceData.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);

	_instanceMatrices.SetDebugName("ModelInstanceMatrices");
	_instanceMatrices.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);

	{
		Renderer::BufferDesc bufferDesc;
		bufferDesc.name = "IndirectArgumentBuffer";
		bufferDesc.usage = Renderer::BufferUsage::INDIRECT_ARGUMENT_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION | Renderer::BufferUsage::STORAGE_BUFFER;
		bufferDesc.size = sizeof(DrawCall);

		_indirectArgumentBuffer = _renderer->CreateBuffer(_culledIndexBuffer, bufferDesc);
		_cullDescriptorSet.Bind("_arguments", _indirectArgumentBuffer);

		// Set InstanceCount to 1
		auto uploadBuffer = _renderer->CreateUploadBuffer(_indirectArgumentBuffer, 0, bufferDesc.size);
		memset(uploadBuffer->mappedMemory, 0, bufferDesc.size);
		static_cast<u32*>(uploadBuffer->mappedMemory)[1] = 1;
	}
}

void ModelRenderer::AddModel(const Model::Header& modelToLoad, std::shared_ptr<Bytebuffer>& buffer, vec3 position, quat rotation, vec3 scale)
{
	_instanceData.WriteLock([&](std::vector<InstanceData>& instanceDatas)
	{
		size_t instanceDataID = instanceDatas.size();

		// Vertex Data
		{
			SafeVectorScopedWriteLock vertexPositionsLock(_vertexPositions);
			SafeVectorScopedWriteLock vertexNormalsLock(_vertexNormals);
			SafeVectorScopedWriteLock vertexUVsLock(_vertexUVs);

			std::vector<vec4>& vertexPositions = vertexPositionsLock.Get();
			std::vector<vec4>& vertexNormals = vertexNormalsLock.Get();
			std::vector<vec2>& vertexUVs = vertexUVsLock.Get();

			size_t verticesBeforeAdd = vertexPositions.size();
			size_t verticesToAdd = modelToLoad.vertices.numElements;

			vertexPositions.resize(verticesBeforeAdd + verticesToAdd);
			vertexNormals.resize(verticesBeforeAdd + verticesToAdd);
			vertexUVs.resize(verticesBeforeAdd + verticesToAdd);

			for (u32 i = 0; i < verticesToAdd; i++)
			{
				const Model::Vertex* vertex = reinterpret_cast<Model::Vertex*>(&buffer->GetDataPointer()[modelToLoad.vertices.bufferOffset] + (i * sizeof(Model::Vertex)));

				vertexPositions[verticesBeforeAdd + i] = vec4(vertex->position, 1.0f);
				vertexNormals[verticesBeforeAdd + i] = vec4(vertex->normal, 1.0f);
				vertexUVs[verticesBeforeAdd + i] = vertex->uv;
			}
		}

		_indices.WriteLock([&](std::vector<u32>& indices)
		{
			for (u32 x = 0; x < 1; x++)
			{
				size_t indicesBeforeAdd = indices.size();
				size_t indicesToAdd = modelToLoad.indices.numElements;

				indices.resize(indicesBeforeAdd + indicesToAdd);
				memcpy(&indices[indicesBeforeAdd], &buffer->GetDataPointer()[modelToLoad.indices.bufferOffset], indicesToAdd * sizeof(u32));
			}
		});

		size_t meshletsBeforeAdd = 0;
		size_t meshletsToAdd = 0;

		_meshlets.WriteLock([&](std::vector<ModelRenderer::Meshlet>& meshlets)
		{
			meshletsBeforeAdd = meshlets.size();
			meshletsToAdd = 0;

			for (u32 i = 0; i < modelToLoad.meshes.numElements; i++)
			{
				ChunkPointer* meshChunkPointer = reinterpret_cast<ChunkPointer*>(&buffer->GetDataPointer()[sizeof(Model::Header) + (i * sizeof(ChunkPointer))]);
				meshletsToAdd += meshChunkPointer->numElements;
			}

			meshlets.resize(meshletsBeforeAdd + meshletsToAdd);

			u32 meshletsAdded = 0;
			for (u32 i = 0; i < modelToLoad.meshes.numElements; i++)
			{
				ChunkPointer* meshChunkPointer = reinterpret_cast<ChunkPointer*>(&buffer->GetDataPointer()[sizeof(Model::Header) + (i * sizeof(ChunkPointer))]);

				for (u32 j = 0; j < meshChunkPointer->numElements; j++)
				{
					Model::Meshlet* modelMeshlet = reinterpret_cast<Model::Meshlet*>(&buffer->GetDataPointer()[meshChunkPointer->bufferOffset + (j * sizeof(Model::Meshlet))]);
					Meshlet& meshlet = meshlets[meshletsBeforeAdd + meshletsAdded + j];

					meshlet.indexStart = modelMeshlet->indexStart;
					meshlet.indexCount = modelMeshlet->indexCount;
					meshlet.instanceDataID = instanceDataID;
				}

				meshletsAdded += meshChunkPointer->numElements;
			}
		});

		InstanceData& instanceData = instanceDatas.emplace_back();
		instanceData.meshletOffset = meshletsBeforeAdd;
		instanceData.meshletCount = meshletsToAdd;
		instanceData.indexOffset = 0;
		instanceData.padding = 0;

		_instanceMatrices.WriteLock([&](std::vector<mat4x4>& instanceMatrices)
		{
			mat4x4& matrix = instanceMatrices.emplace_back();

			mat4x4 rotationMatrix = glm::toMat4(rotation);
			mat4x4 scaleMatrix = glm::scale(mat4x4(1.0f), scale);
			matrix = glm::translate(mat4x4(1.0f), position) * rotationMatrix * scaleMatrix;
		});

		DebugHandler::PrintSuccess("ModelRenderer : Added Model (Vertices : %u, Indices : %u, Meshes : %u, Meshlets : %u)", modelToLoad.vertices.numElements, modelToLoad.indices.numElements, modelToLoad.meshes.numElements, meshletsToAdd);
	});
}

void ModelRenderer::SyncBuffers()
{
	if (_indices.SyncToGPU(_renderer))
	{
		Renderer::BufferDesc bufferDesc;
		bufferDesc.name = "CulledIndexBuffer";
		bufferDesc.usage = Renderer::BufferUsage::INDEX_BUFFER | Renderer::BufferUsage::STORAGE_BUFFER;
		bufferDesc.size = _indices.Size() * sizeof(u32);

		_culledIndexBuffer = _renderer->CreateBuffer(_culledIndexBuffer, bufferDesc);

		_drawDescriptorSet.Bind("_indices", _indices.GetBuffer());
		_cullDescriptorSet.Bind("_indices", _indices.GetBuffer());
		_cullDescriptorSet.Bind("_culledIndices", _culledIndexBuffer);
	}

	if (_vertexPositions.SyncToGPU(_renderer))
	{
		_drawDescriptorSet.Bind("_vertexPositions", _vertexPositions.GetBuffer());
	}

	if (_vertexNormals.SyncToGPU(_renderer))
	{
		_drawDescriptorSet.Bind("_vertexNormals", _vertexNormals.GetBuffer());
	}

	if (_vertexUVs.SyncToGPU(_renderer))
	{
		_drawDescriptorSet.Bind("_vertexUVs", _vertexUVs.GetBuffer());
	}

	if (_meshlets.SyncToGPU(_renderer))
	{
		_drawDescriptorSet.Bind("_meshlets", _meshlets.GetBuffer());
		_cullDescriptorSet.Bind("_meshlets", _meshlets.GetBuffer());
	}

	if (_instanceData.SyncToGPU(_renderer))
	{
		_drawDescriptorSet.Bind("_instanceDatas", _instanceData.GetBuffer());
		_cullDescriptorSet.Bind("_instanceDatas", _instanceData.GetBuffer());
	}

	if (_instanceMatrices.SyncToGPU(_renderer))
	{
		_drawDescriptorSet.Bind("_instanceMatrices", _instanceMatrices.GetBuffer());
		_cullDescriptorSet.Bind("_instanceMatrices", _instanceMatrices.GetBuffer());
	}
}

void ModelRenderer::Update(f32 deltaTime)
{
	SyncBuffers();
}

void ModelRenderer::AddCullingPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex)
{
	size_t numMeshlets = _meshlets.Size();
	if (numMeshlets == 0)
		return;

	struct Data { };

	renderGraph->AddPass<Data>("Culling",
		[=](Data& data, Renderer::RenderGraphBuilder& builder) // Setup
		{
			return true; // Return true from setup to enable this pass, return false to disable it
		},
		[=](Data& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList) // Execute
		{
			GPU_SCOPED_PROFILER_ZONE(commandList, CullingPass);

			Renderer::ComputePipelineDesc pipelineDesc;
			graphResources.InitializePipelineDesc(pipelineDesc);

			// Culling Shader
			Renderer::ComputeShaderDesc shaderDesc;
			shaderDesc.path = "Model/Culling.cs.hlsl";
			pipelineDesc.computeShader = _renderer->LoadShader(shaderDesc);

			commandList.FillBuffer(_indirectArgumentBuffer, 0, 4, 0);
			//commandList.FillBuffer(_indirectArgumentBuffer, 0, 4, _indices.Size());
			commandList.PipelineBarrier(Renderer::PipelineBarrierType::TransferDestToComputeShaderRW, _indirectArgumentBuffer);

			Renderer::ComputePipelineID activePipeline = _renderer->CreatePipeline(pipelineDesc);
			commandList.BeginPipeline(activePipeline);
			{
				commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::PER_DRAW, &_cullDescriptorSet, frameIndex);

				struct Constants
				{
					u32 numMeshletsTotal;
					u32 numMeshletsPerThread;
				};

				Constants* constants = graphResources.FrameNew<Constants>();
				constants->numMeshletsTotal = numMeshlets;
				constants->numMeshletsPerThread = 32;
				commandList.PushConstant(constants, 0, sizeof(Constants));

				u32 meshletWorkDispatchs = Renderer::GetDispatchCount(constants->numMeshletsTotal, constants->numMeshletsPerThread);
				u32 dispatchCount = Renderer::GetDispatchCount(meshletWorkDispatchs, 64);
				commandList.Dispatch(dispatchCount, 1, 1);
			}
			commandList.EndPipeline(activePipeline);
		});
}

void ModelRenderer::AddGeometryPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex)
{
	size_t numMeshlets = _meshlets.Size();
	if (numMeshlets == 0)
		return;

	struct Data
	{
		Renderer::RenderPassMutableResource color;
		Renderer::RenderPassMutableResource depth;
	};

	renderGraph->AddPass<Data>("Geometry",
		[=, &resources](Data& data, Renderer::RenderGraphBuilder& builder) // Setup
		{
			data.color = builder.Write(resources.finalColor, Renderer::RenderGraphBuilder::WriteMode::RENDERTARGET, Renderer::RenderGraphBuilder::LoadMode::LOAD);
			data.depth = builder.Write(resources.depth, Renderer::RenderGraphBuilder::WriteMode::RENDERTARGET, Renderer::RenderGraphBuilder::LoadMode::LOAD);

			return true; // Return true from setup to enable this pass, return false to disable it
		},
		[=, &resources](Data& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList) // Execute
		{
			GPU_SCOPED_PROFILER_ZONE(commandList, GeometryPass);

			Renderer::GraphicsPipelineDesc pipelineDesc;
			graphResources.InitializePipelineDesc(pipelineDesc);

			// Rasterizer state
			pipelineDesc.states.rasterizerState.cullMode = Renderer::CullMode::BACK;
			//pipelineDesc.states.rasterizerState.frontFaceMode = Renderer::FrontFaceState::COUNTERCLOCKWISE;

			pipelineDesc.depthStencil = data.depth;
			pipelineDesc.states.depthStencilState.depthEnable = true;
			pipelineDesc.states.depthStencilState.depthWriteEnable = true;
			pipelineDesc.states.depthStencilState.depthFunc = Renderer::ComparisonFunc::GREATER;

			// Render targets
			pipelineDesc.renderTargets[0] = data.color;

			// Panel Shaders
			Renderer::VertexShaderDesc vertexShaderDesc;
			vertexShaderDesc.path = "Model/Draw.vs.hlsl";
			pipelineDesc.states.vertexShader = _renderer->LoadShader(vertexShaderDesc);

			Renderer::PixelShaderDesc pixelShaderDesc;
			pixelShaderDesc.path = "Model/Draw.ps.hlsl";
			pixelShaderDesc.AddPermutationField("WIREFRAME", "0");
			pipelineDesc.states.pixelShader = _renderer->LoadShader(pixelShaderDesc);

			Renderer::GraphicsPipelineID activePipeline = _renderer->CreatePipeline(pipelineDesc);

			commandList.ImageBarrier(resources.finalColor);

			commandList.PipelineBarrier(Renderer::PipelineBarrierType::ComputeWriteToIndirectArguments, _indirectArgumentBuffer);
			commandList.PipelineBarrier(Renderer::PipelineBarrierType::ComputeWriteToVertexShaderRead, _culledIndexBuffer);
			commandList.PipelineBarrier(Renderer::PipelineBarrierType::ComputeWriteToIndexBuffer, _culledIndexBuffer);

			commandList.BeginPipeline(activePipeline);
			{
				commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::GLOBAL, &resources.globalDescriptorSet, frameIndex);
				commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::PER_DRAW, &_drawDescriptorSet, frameIndex);

				commandList.SetIndexBuffer(_culledIndexBuffer, Renderer::IndexFormat::UInt32);
				commandList.DrawIndexedIndirect(_indirectArgumentBuffer, 0, 1);
			}
			commandList.EndPipeline(activePipeline);

			// Wireframe
			{
				pipelineDesc.states.rasterizerState.fillMode = Renderer::FillMode::WIREFRAME;

				Renderer::PixelShaderDesc pixelShaderDesc;
				pixelShaderDesc.path = "Model/Draw.ps.hlsl";
				pixelShaderDesc.AddPermutationField("WIREFRAME", "1");
				pipelineDesc.states.pixelShader = _renderer->LoadShader(pixelShaderDesc);

				Renderer::GraphicsPipelineID activePipeline = _renderer->CreatePipeline(pipelineDesc);

				commandList.BeginPipeline(activePipeline);
				{
					commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::GLOBAL, &resources.globalDescriptorSet, frameIndex);
					commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::PER_DRAW, &_drawDescriptorSet, frameIndex);

					commandList.SetIndexBuffer(_culledIndexBuffer, Renderer::IndexFormat::UInt32);
					commandList.DrawIndexedIndirect(_indirectArgumentBuffer, 0, 1);

					//commandList.SetIndexBuffer(_indices.GetBuffer(), Renderer::IndexFormat::UInt32);
					//commandList.DrawIndexed(_indices.Size(), 1, 0, 0, 0);
				}
				commandList.EndPipeline(activePipeline);
			}
		});
}