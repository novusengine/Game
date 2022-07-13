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
	_vertices.SetDebugName("ModelVertices");
	_vertices.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);

	_indices.SetDebugName("ModelIndices");
	_indices.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);

	_meshlets.SetDebugName("ModelMeshlets");
	_meshlets.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);

	_instanceData.SetDebugName("ModelInstanceData");
	_instanceData.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);

	{
		Renderer::BufferDesc bufferDesc;
		bufferDesc.name = "IndirectArgumentBuffer";
		bufferDesc.usage = Renderer::BufferUsage::INDIRECT_ARGUMENT_BUFFER | Renderer::BufferUsage::STORAGE_BUFFER;
		bufferDesc.size = sizeof(DrawCall);

		_indirectArgumentBuffer = _renderer->CreateBuffer(_culledIndexBuffer, bufferDesc);
		_cullDescriptorSet.Bind("_arguments", _indirectArgumentBuffer);

		// Set InstanceCount to 1
		auto uploadBuffer = _renderer->CreateUploadBuffer(_indirectArgumentBuffer, 0, bufferDesc.size);
		memset(uploadBuffer->mappedMemory, 0, bufferDesc.size);
		static_cast<u32*>(uploadBuffer->mappedMemory)[1] = 1;
	}
}

void ModelRenderer::AddModel(const Model::Header& modelToLoad, std::shared_ptr<Bytebuffer>& buffer)
{
	_vertices.WriteLock([&](std::vector<ModelRenderer::Vertex>& vertices)
	{
		size_t verticesBeforeAdd = vertices.size();
		size_t verticesToAdd = modelToLoad.vertices.numElements;

		vertices.resize(verticesBeforeAdd + verticesToAdd);
		memcpy(&vertices[verticesBeforeAdd], &buffer->GetDataPointer()[modelToLoad.vertices.bufferOffset], verticesToAdd * sizeof(ModelRenderer::Vertex));
	});

	_indices.WriteLock([&](std::vector<u32>& indices)
	{
		size_t indicesBeforeAdd = indices.size();
		size_t indicesToAdd = modelToLoad.indices.numElements;

		indices.resize(indicesBeforeAdd + indicesToAdd);
		memcpy(&indices[indicesBeforeAdd], &buffer->GetDataPointer()[modelToLoad.indices.bufferOffset], indicesToAdd * sizeof(u32));
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
				Model::Meshlet* modelMeshlet = reinterpret_cast<Model::Meshlet*>(&buffer->GetDataPointer()[meshChunkPointer->bufferOffset + (i * sizeof(Model::Meshlet))]);
				Meshlet& meshlet = meshlets[meshletsBeforeAdd + meshletsAdded + j];

				meshlet.indexStart = modelMeshlet->indexStart;
				meshlet.indexCount = modelMeshlet->indexCount;
			}

			meshletsAdded += meshChunkPointer->numElements;
		}


		//memcpy(&meshlets[meshletsBeforeAdd], &buffer->GetDataPointer()[meshChunkPointer->bufferOffset], meshletsToAdd * sizeof(ModelRenderer::Meshlet));
	});

	_instanceData.WriteLock([&](std::vector<InstanceData>& instanceDatas)
	{
		InstanceData& instanceData = instanceDatas.emplace_back();
		instanceData.meshletOffset = meshletsBeforeAdd;
		instanceData.meshletCount = meshletsToAdd;
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

		_cullDescriptorSet.Bind("_indices", _indices.GetBuffer());
		_cullDescriptorSet.Bind("_culledIndices", _culledIndexBuffer);
	}

	if (_vertices.SyncToGPU(_renderer))
	{
		_drawDescriptorSet.Bind("_vertices", _vertices.GetBuffer());
	}

	if (_meshlets.SyncToGPU(_renderer))
	{
		_cullDescriptorSet.Bind("_meshlets", _meshlets.GetBuffer());
	}

	if (_instanceData.SyncToGPU(_renderer))
	{
		_cullDescriptorSet.Bind("_instanceDatas", _instanceData.GetBuffer());
	}
}

void ModelRenderer::Update(f32 deltaTime)
{
	SyncBuffers();
}

void ModelRenderer::AddCullingPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex)
{
	size_t numInstances = _instanceData.Size();
	if (numInstances == 0)
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
			commandList.PipelineBarrier(Renderer::PipelineBarrierType::TransferDestToComputeShaderRW, _indirectArgumentBuffer);

			Renderer::ComputePipelineID activePipeline = _renderer->CreatePipeline(pipelineDesc);
			commandList.BeginPipeline(activePipeline);
			{
				commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::PER_DRAW, &_cullDescriptorSet, frameIndex);

				struct Constants
				{
					u32 numInstances;
				};

				Constants* constants = graphResources.FrameNew<Constants>();
				constants->numInstances = numInstances;
				commandList.PushConstant(constants, 0, sizeof(Constants));

				u32 dispatchCount = Renderer::GetDispatchCount(numInstances, 32);
				commandList.Dispatch(dispatchCount, 1, 1);
			}
			commandList.EndPipeline(activePipeline);
		});
}

void ModelRenderer::AddGeometryPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex)
{
	size_t numInstances = _instanceData.Size();
	if (numInstances == 0)
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
		});
}