#include "ModelRenderer.h"
#include "Game/Rendering/RenderResources.h"

#include <Base/CVarSystem/CVarSystem.h>
#include <Base/Memory/Bytebuffer.h>

#include <Renderer/RenderGraph.h>

AutoCVar_Int CVAR_ModelRendererWireFrameMode("modelRenderer.wireFrameMode", "enable wireframe for models", 0, CVarFlags::EditCheckbox);

ModelRenderer::ModelRenderer(Renderer::Renderer* renderer) : _renderer(renderer)
{
	CreatePermanentResources();
}

void ModelRenderer::CreatePermanentResources()
{
	Renderer::SamplerDesc samplerDesc;
	samplerDesc.enabled = true;
	samplerDesc.filter = Renderer::SamplerFilter::MIN_MAG_MIP_POINT;
	samplerDesc.addressU = Renderer::TextureAddressMode::WRAP;
	samplerDesc.addressV = Renderer::TextureAddressMode::WRAP;
	samplerDesc.addressW = Renderer::TextureAddressMode::WRAP;
	samplerDesc.shaderVisibility = Renderer::ShaderVisibility::PIXEL;

	_sampler = _renderer->CreateSampler(samplerDesc);
	_drawDescriptorSet.Bind("_sampler"_h, _sampler);

	_vertexPositions.SetDebugName("ModelVertexPositions");
	_vertexPositions.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);

	_vertexNormals.SetDebugName("ModelVertexNormals");
	_vertexNormals.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);

	_vertexUVs.SetDebugName("ModelVertexUVs");
	_vertexUVs.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);

	_indices.SetDebugName("ModelIndices");
	_indices.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);

	_culledIndices.SetDebugName("ModelCulledIndices");
	_culledIndices.SetUsage(Renderer::BufferUsage::INDEX_BUFFER | Renderer::BufferUsage::STORAGE_BUFFER);

	_culledIndices.WriteLock([&](std::vector<u32>& culledIndicies)
	{
		culledIndicies.resize(50000000);
	});

	_meshlets.SetDebugName("ModelMeshlets");
	_meshlets.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);

	_modelData.SetDebugName("ModelData");
	_modelData.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);

	_instanceData.SetDebugName("ModelInstanceData");
	_instanceData.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);

	_survivedInstanceDatas.SetDebugName("ModelSurvivedInstanceData");
	_survivedInstanceDatas.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);

	_instanceMatrices.SetDebugName("ModelInstanceMatrices");
	_instanceMatrices.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);

	{
		Renderer::BufferDesc bufferDesc;
		bufferDesc.name = "IndirectDrawArgumentBuffer";
		bufferDesc.usage = Renderer::BufferUsage::INDIRECT_ARGUMENT_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION | Renderer::BufferUsage::STORAGE_BUFFER;
		bufferDesc.size = sizeof(DrawCall);

		_indirectDrawArgumentBuffer = _renderer->CreateBuffer(_indirectDrawArgumentBuffer, bufferDesc);
		_cullDescriptorSet.Bind("_drawArguments", _indirectDrawArgumentBuffer);

		// Set InstanceCount to 1
		auto uploadBuffer = _renderer->CreateUploadBuffer(_indirectDrawArgumentBuffer, 0, bufferDesc.size);
		memset(uploadBuffer->mappedMemory, 0, bufferDesc.size);
		static_cast<u32*>(uploadBuffer->mappedMemory)[1] = 1;
	}

	{
		Renderer::BufferDesc bufferDesc;
		bufferDesc.name = "IndirectDispatchArgumentBuffer";
		bufferDesc.usage = Renderer::BufferUsage::INDIRECT_ARGUMENT_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION | Renderer::BufferUsage::STORAGE_BUFFER;
		bufferDesc.size = sizeof(PaddedDispatch);

		_indirectDispatchArgumentBuffer = _renderer->CreateBuffer(_indirectDispatchArgumentBuffer, bufferDesc);
		_cullDescriptorSet.Bind("_dispatchArguments", _indirectDispatchArgumentBuffer);

		// Set y, z to 1
		auto uploadBuffer = _renderer->CreateUploadBuffer(_indirectDispatchArgumentBuffer, 0, bufferDesc.size);
		memset(uploadBuffer->mappedMemory, 0, bufferDesc.size);
		static_cast<u32*>(uploadBuffer->mappedMemory)[2] = 1; // Y
		static_cast<u32*>(uploadBuffer->mappedMemory)[3] = 1; // Z
	}

	{
		Renderer::BufferDesc bufferDesc;
		bufferDesc.name = "MeshletInstanceBuffer";
		bufferDesc.usage = Renderer::BufferUsage::STORAGE_BUFFER;
		bufferDesc.size = sizeof(MeshletInstance) * 1000000; // TODO : GPU Only Vector??? This needs to be worst case allocated, calculated every time we add an instance (CurrentSize + (Instance * numMeshletsPerInstance));

		_meshletInstanceBuffer = _renderer->CreateBuffer(_meshletInstanceBuffer, bufferDesc);
		_cullDescriptorSet.Bind("_meshletInstances", _meshletInstanceBuffer);
		_drawDescriptorSet.Bind("_meshletInstances", _meshletInstanceBuffer);
	}

	{
		Renderer::BufferDesc bufferDesc;
		bufferDesc.name = "MeshletInstanceCountBuffer";
		bufferDesc.usage = Renderer::BufferUsage::TRANSFER_DESTINATION | Renderer::BufferUsage::STORAGE_BUFFER;
		bufferDesc.size = sizeof(u32);

		_meshletInstanceCountBuffer = _renderer->CreateBuffer(_meshletInstanceCountBuffer, bufferDesc);
		_cullDescriptorSet.Bind("_meshletInstanceCount", _meshletInstanceCountBuffer);
	}

	Renderer::TextureArrayDesc textureArrayDesc;
	textureArrayDesc.size = 4096;

	_textures = _renderer->CreateTextureArray(textureArrayDesc);
	_drawDescriptorSet.Bind("_textures"_h, _textures);

	CreateCheckerboardCube(Color::White, Color::Blue);
	CreateCheckerboardCube(Color::White, Color::Red);
}

void ModelRenderer::SyncBuffers()
{
	if (_indices.SyncToGPU(_renderer))
	{
		_drawDescriptorSet.Bind("_indices", _indices.GetBuffer());
		_cullDescriptorSet.Bind("_indices", _indices.GetBuffer());
	}

	if (_culledIndices.SyncToGPU(_renderer))
	{
		_cullDescriptorSet.Bind("_culledIndices", _culledIndices.GetBuffer());
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

	if (_modelData.SyncToGPU(_renderer))
	{
		_drawDescriptorSet.Bind("_modelDatas", _modelData.GetBuffer());
		_cullDescriptorSet.Bind("_modelDatas", _modelData.GetBuffer());
	}

	if (_instanceData.SyncToGPU(_renderer))
	{
		_drawDescriptorSet.Bind("_instanceDatas", _instanceData.GetBuffer());
		_cullDescriptorSet.Bind("_instanceDatas", _instanceData.GetBuffer());
	}

	if (_survivedInstanceDatas.SyncToGPU(_renderer))
	{
		_drawDescriptorSet.Bind("_survivedInstanceDatas", _survivedInstanceDatas.GetBuffer());
		_cullDescriptorSet.Bind("_survivedInstanceDatas", _survivedInstanceDatas.GetBuffer());
	}

	if (_instanceMatrices.SyncToGPU(_renderer))
	{
		_drawDescriptorSet.Bind("_instanceMatrices", _instanceMatrices.GetBuffer());
		_cullDescriptorSet.Bind("_instanceMatrices", _instanceMatrices.GetBuffer());
	}

	if (_meshlets.SyncToGPU(_renderer))
	{
		_drawDescriptorSet.Bind("_meshletDatas", _meshlets.GetBuffer());
		_cullDescriptorSet.Bind("_meshletDatas", _meshlets.GetBuffer());
	}
}

void ModelRenderer::CreateCheckerboardCube(Color color1, Color color2)
{
	constexpr u32 TEXTURE_RESOLUTION = 8;
	constexpr u32 TEXTURE_SIZE = sizeof(u32) * (TEXTURE_RESOLUTION * TEXTURE_RESOLUTION);
	static u8 TEXTURE_DATA[TEXTURE_SIZE] = { 0 };

	bool isColor1 = true;

	for (u32 i = 0; i < TEXTURE_RESOLUTION * TEXTURE_RESOLUTION; i++)
	{
		Color result = isColor1 ? color1 : color2;

		u32 offset = i * sizeof(u32);
		TEXTURE_DATA[offset + 0] = static_cast<u8>(result.r * 255.0f);
		TEXTURE_DATA[offset + 1] = static_cast<u8>(result.g * 255.0f);
		TEXTURE_DATA[offset + 2] = static_cast<u8>(result.b * 255.0f);
		TEXTURE_DATA[offset + 3] = static_cast<u8>(result.a * 255.0f);

		if (i % TEXTURE_RESOLUTION != TEXTURE_RESOLUTION - 1)
		{
			isColor1 = !isColor1;
		}
	}

	Renderer::DataTextureDesc dataTextureDesc;
	dataTextureDesc.width = TEXTURE_RESOLUTION;
	dataTextureDesc.height = TEXTURE_RESOLUTION;
	dataTextureDesc.format = Renderer::ImageFormat::R8G8B8A8_UNORM;
	dataTextureDesc.data = new u8[TEXTURE_SIZE];
	dataTextureDesc.debugName = "CheckerboardCubeTexture";

	memcpy(dataTextureDesc.data, &TEXTURE_DATA[0], TEXTURE_SIZE);

	u32 arrayIndex = 0;
	_renderer->CreateDataTextureIntoArray(dataTextureDesc, _textures, arrayIndex);

	static u32 count = 0;
	std::string virtualModelPath = "VirtualModel/Cube-(0.0f, 0.0f, 0.0f, 0.0f)-(0.0f, 0.0f, 0.0f, 0.0f)" + std::to_string(count++);
	u32 virtualModelHash = StringUtils::fnv1a_32(virtualModelPath.c_str(), virtualModelPath.length());

	_modelHashToModelID.WriteLock([&](robin_hood::unordered_map<u32, u32>& modelHashToModelID)
	{
		auto itr = modelHashToModelID.find(virtualModelHash);
		if (itr == modelHashToModelID.end())
		{
			u32 modelDataID = std::numeric_limits<u32>().max();
			ModelData* modelDataPtr = nullptr;

			_modelData.WriteLock([&](std::vector<ModelData>& modelData)
			{
				modelDataID = modelData.size();
				modelDataPtr = &modelData.emplace_back();
			});

			size_t verticesBeforeAdd = 0;
			size_t verticesToAdd = 36;
			// Vertex Data
			{
				_vertexPositions.WriteLock([&](std::vector<vec4>& vertexPositions)
				{
					verticesBeforeAdd = vertexPositions.size();
					vertexPositions.reserve(verticesBeforeAdd + verticesToAdd);

					vertexPositions.push_back(vec4(-1.0f, 1.0f, -1.0f, 1.0f));
					vertexPositions.push_back(vec4(-1.0f, 1.0f, 1.0f,  1.0f));
					vertexPositions.push_back(vec4(1.0f,  1.0f, 1.0f,  1.0f));
					vertexPositions.push_back(vec4(1.0f,  1.0f, 1.0f,  1.0f));

					vertexPositions.push_back(vec4(-1.0f, 1.0f,  1.0f,  1.0f));
					vertexPositions.push_back(vec4(-1.0f, -1.0f, 1.0f,  1.0f));
					vertexPositions.push_back(vec4(-1.0f, -1.0f, 1.0f,  1.0f));
					vertexPositions.push_back(vec4(-1.0f, 1.0f,  1.0f,  1.0f));

					vertexPositions.push_back(vec4(-1.0f, -1.0f, -1.0f, 1.0f));
					vertexPositions.push_back(vec4(-1.0f, 1.0f,  1.0f,  1.0f));
					vertexPositions.push_back(vec4(-1.0f, 1.0f,  -1.0f, 1.0f));
					vertexPositions.push_back(vec4(-1.0f, -1.0f, -1.0f, 1.0f));

					vertexPositions.push_back(vec4(1.0f,  -1.0f, 1.0f,  1.0f));
					vertexPositions.push_back(vec4(1.0f,  1.0f,  1.0f,  1.0f));
					vertexPositions.push_back(vec4(-1.0f, -1.0f, 1.0f,  1.0f));
					vertexPositions.push_back(vec4(1.0f,  1.0f,  -1.0f, 1.0f));

					vertexPositions.push_back(vec4(-1.0f, 1.0f, -1.0f, 1.0f));
					vertexPositions.push_back(vec4(1.0f,  1.0f, 1.0f,  1.0f));
					vertexPositions.push_back(vec4(1.0f,  1.0f, -1.0f, 1.0f));
					vertexPositions.push_back(vec4(1.0f,  1.0f, 1.0f,  1.0f));

					vertexPositions.push_back(vec4(1.0f,  -1.0f, 1.0f,  1.0f));
					vertexPositions.push_back(vec4(-1.0f, 1.0f,  -1.0f, 1.0f));
					vertexPositions.push_back(vec4(1.0f,  1.0f,  -1.0f, 1.0f));
					vertexPositions.push_back(vec4(1.0f,  -1.0f, -1.0f, 1.0f));

					vertexPositions.push_back(vec4(-1.0f, -1.0f, -1.0f, 1.0f));
					vertexPositions.push_back(vec4(-1.0f, 1.0f,  -1.0f, 1.0f));
					vertexPositions.push_back(vec4(1.0f,  -1.0f, -1.0f, 1.0f));
					vertexPositions.push_back(vec4(1.0f,  -1.0f, -1.0f, 1.0f));

					vertexPositions.push_back(vec4(1.0f,  1.0f,  -1.0f, 1.0f));
					vertexPositions.push_back(vec4(1.0f,  -1.0f,  1.0f, 1.0f));
					vertexPositions.push_back(vec4(-1.0f, -1.0f, -1.0f, 1.0f));
					vertexPositions.push_back(vec4(1.0f,  -1.0f, -1.0f, 1.0f));

					vertexPositions.push_back(vec4(-1.0f, -1.0f, 1.0f,  1.0f));
					vertexPositions.push_back(vec4(1.0f,  -1.0f, -1.0f, 1.0f));
					vertexPositions.push_back(vec4(1.0f,  -1.0f, 1.0f,  1.0f));
					vertexPositions.push_back(vec4(-1.0f, -1.0f, 1.0f,  1.0f));
				});
				_vertexNormals.WriteLock([&](std::vector<vec4>& vertexNormals)
				{
					vertexNormals.reserve(verticesBeforeAdd + verticesToAdd);
					vertexNormals.push_back(vec4(0.0f, 1.0f, 0.0f, 1.0f));
					vertexNormals.push_back(vec4(0.0f, 1.0f, 0.0f, 1.0f));
					vertexNormals.push_back(vec4(0.0f, 1.0f, 0.0f, 1.0f));
					vertexNormals.push_back(vec4(0.0f, 0.0f, 1.0f, 1.0f));

					vertexNormals.push_back(vec4(0.0f,  0.0f, 1.0f, 1.0f));
					vertexNormals.push_back(vec4(0.0f,  0.0f, 1.0f, 1.0f));
					vertexNormals.push_back(vec4(-1.0f, 0.0f, 0.0f, 1.0f));
					vertexNormals.push_back(vec4(-1.0f, 0.0f, 0.0f, 1.0f));

					vertexNormals.push_back(vec4(-1.0f, 0.0f, 0.0f, 1.0f));
					vertexNormals.push_back(vec4(-1.0f, 0.0f, 0.0f, 1.0f));
					vertexNormals.push_back(vec4(-1.0f, 0.0f, 0.0f, 1.0f));
					vertexNormals.push_back(vec4(-1.0f, 0.0f, 0.0f, 1.0f));

					vertexNormals.push_back(vec4(0.0f, 0.0f, 1.0f, 1.0f));
					vertexNormals.push_back(vec4(0.0f, 0.0f, 1.0f, 1.0f));
					vertexNormals.push_back(vec4(0.0f, 0.0f, 1.0f, 1.0f));
					vertexNormals.push_back(vec4(0.0f, 1.0f, 0.0f, 1.0f));

					vertexNormals.push_back(vec4(0.0f, 1.0f, 0.0f, 1.0f));
					vertexNormals.push_back(vec4(0.0f, 1.0f, 0.0f, 1.0f));
					vertexNormals.push_back(vec4(1.0f, 0.0f, 0.0f, 1.0f));
					vertexNormals.push_back(vec4(1.0f, 0.0f, 0.0f, 1.0f));

					vertexNormals.push_back(vec4(1.0f, 0.0f, 0.0f,  1.0f));
					vertexNormals.push_back(vec4(0.0f, 0.0f, -1.0f, 1.0f));
					vertexNormals.push_back(vec4(0.0f, 0.0f, -1.0f, 1.0f));
					vertexNormals.push_back(vec4(0.0f, 0.0f, -1.0f, 1.0f));

					vertexNormals.push_back(vec4(0.0f, 0.0f, -1.0f, 1.0f));
					vertexNormals.push_back(vec4(0.0f, 0.0f, -1.0f, 1.0f));
					vertexNormals.push_back(vec4(0.0f, 0.0f, -1.0f, 1.0f));
					vertexNormals.push_back(vec4(1.0f, 0.0f, 0.0f,  1.0f));

					vertexNormals.push_back(vec4(1.0f, 0.0f,  0.0f, 1.0f));
					vertexNormals.push_back(vec4(1.0f, 0.0f,  0.0f, 1.0f));
					vertexNormals.push_back(vec4(0.0f, -1.0f, 0.0f, 1.0f));
					vertexNormals.push_back(vec4(0.0f, -1.0f, 0.0f, 1.0f));

					vertexNormals.push_back(vec4(0.0f, -1.0f, 0.0f, 1.0f));
					vertexNormals.push_back(vec4(0.0f, -1.0f, 0.0f, 1.0f));
					vertexNormals.push_back(vec4(0.0f, -1.0f, 0.0f, 1.0f));
					vertexNormals.push_back(vec4(0.0f, -1.0f, 0.0f, 1.0f));
				});
				_vertexUVs.WriteLock([&](std::vector<vec2>& vertexUVs)
				{
					vertexUVs.reserve(verticesBeforeAdd + verticesToAdd);

					f32 scaleFactor = 4.0f;
					vertexUVs.push_back(vec2(0.875f, 0.50f) * scaleFactor);
					vertexUVs.push_back(vec2(0.875f, 0.75f) * scaleFactor);
					vertexUVs.push_back(vec2(0.625f, 0.75f) * scaleFactor);
					vertexUVs.push_back(vec2(0.625f, 0.75f) * scaleFactor);
					vertexUVs.push_back(vec2(0.625f, 1.0f)	* scaleFactor);
					vertexUVs.push_back(vec2(0.375f, 1.0f)	* scaleFactor);
					vertexUVs.push_back(vec2(0.375f, 0.0f)	* scaleFactor);
					vertexUVs.push_back(vec2(0.625f, 0.0f)	* scaleFactor);
					vertexUVs.push_back(vec2(0.375f, 0.25f) * scaleFactor);
					vertexUVs.push_back(vec2(0.625f, 0.0f)	* scaleFactor);
					vertexUVs.push_back(vec2(0.625f, 0.25f) * scaleFactor);
					vertexUVs.push_back(vec2(0.375f, 0.25f) * scaleFactor);
					vertexUVs.push_back(vec2(0.375f, 0.75f) * scaleFactor);
					vertexUVs.push_back(vec2(0.625f, 0.75f) * scaleFactor);
					vertexUVs.push_back(vec2(0.375f, 1.0f)	* scaleFactor);
					vertexUVs.push_back(vec2(0.625f, 0.50f) * scaleFactor);
					vertexUVs.push_back(vec2(0.875f, 0.50f) * scaleFactor);
					vertexUVs.push_back(vec2(0.625f, 0.75f) * scaleFactor);
					vertexUVs.push_back(vec2(0.625f, 0.50f) * scaleFactor);
					vertexUVs.push_back(vec2(0.625f, 0.75f) * scaleFactor);
					vertexUVs.push_back(vec2(0.375f, 0.75f) * scaleFactor);
					vertexUVs.push_back(vec2(0.625f, 0.25f) * scaleFactor);
					vertexUVs.push_back(vec2(0.625f, 0.50f) * scaleFactor);
					vertexUVs.push_back(vec2(0.375f, 0.50f) * scaleFactor);
					vertexUVs.push_back(vec2(0.375f, 0.25f) * scaleFactor);
					vertexUVs.push_back(vec2(0.625f, 0.25f) * scaleFactor);
					vertexUVs.push_back(vec2(0.375f, 0.50f) * scaleFactor);
					vertexUVs.push_back(vec2(0.375f, 0.50f) * scaleFactor);
					vertexUVs.push_back(vec2(0.625f, 0.50f) * scaleFactor);
					vertexUVs.push_back(vec2(0.375f, 0.75f) * scaleFactor);
					vertexUVs.push_back(vec2(0.125f, 0.50f) * scaleFactor);
					vertexUVs.push_back(vec2(0.375f, 0.50f) * scaleFactor);
					vertexUVs.push_back(vec2(0.125f, 0.75f) * scaleFactor);
					vertexUVs.push_back(vec2(0.375f, 0.50f) * scaleFactor);
					vertexUVs.push_back(vec2(0.375f, 0.75f) * scaleFactor);
					vertexUVs.push_back(vec2(0.125f, 0.75f) * scaleFactor);
				});
			}

			size_t indicesBeforeAdd = 0;
			size_t indicesToAdd = 36;
			_indices.WriteLock([&](std::vector<u32>& indices)
			{
				indicesBeforeAdd = indices.size();
				indices.reserve(indicesBeforeAdd + indicesToAdd);

				for (u32 i = 0; i < indicesToAdd; i++)
				{
					indices.push_back(i);
				}
			});

			size_t meshletDataBeforeAdd = 0;
			size_t meshletDataToAdd = 1;
			_meshlets.WriteLock([&](std::vector<ModelRenderer::MeshletData>& meshletDatas)
			{
				meshletDataBeforeAdd = meshletDatas.size();
				MeshletData& meshletData = meshletDatas.emplace_back();

				meshletData.indexStart = 0;
				meshletData.indexCount = indicesToAdd;
				assert(meshletData.indexCount % 3 == 0);
			});

			modelDataPtr->meshletOffset = meshletDataBeforeAdd;
			modelDataPtr->meshletCount = meshletDataToAdd;
			modelDataPtr->textureID = arrayIndex;
			modelDataPtr->indexOffset = indicesBeforeAdd;
			modelDataPtr->vertexOffset = verticesBeforeAdd;

			modelHashToModelID[virtualModelHash] = modelDataID;

			DebugHandler::PrintSuccess("ModelRenderer : Added Model (Vertices : %u, Indices : %u, Meshes : %u, Meshlets : %u)", verticesToAdd, indicesToAdd, 1, meshletDataToAdd);
		}
	});
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
			shaderDesc.path = "Model/CullInstances.cs.hlsl";
			pipelineDesc.computeShader = _renderer->LoadShader(shaderDesc);

			commandList.FillBuffer(_meshletInstanceCountBuffer, 0, 4, 0);
			commandList.FillBuffer(_indirectDrawArgumentBuffer, 0, 4, 0);
			commandList.FillBuffer(_indirectDispatchArgumentBuffer, 0, 8, 0);
			commandList.PipelineBarrier(Renderer::PipelineBarrierType::TransferDestToComputeShaderRW, _indirectDispatchArgumentBuffer);

			Renderer::ComputePipelineID activePipeline = _renderer->CreatePipeline(pipelineDesc);
			
			{
				commandList.BeginPipeline(activePipeline);

				commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::PER_DRAW, &_cullDescriptorSet, frameIndex);

				struct Constants
				{
					u32 numInstances;
				};

				Constants* constants = graphResources.FrameNew<Constants>();
				constants->numInstances = numInstances;
				commandList.PushConstant(constants, 0, sizeof(Constants));

				u32 dispatchCount = Renderer::GetDispatchCount(numInstances, 64);
				commandList.Dispatch(dispatchCount, 1, 1);

				commandList.EndPipeline(activePipeline);
			}

			commandList.PipelineBarrier(Renderer::PipelineBarrierType::TransferDestToComputeShaderRW, _indirectDrawArgumentBuffer);
			commandList.PipelineBarrier(Renderer::PipelineBarrierType::TransferDestToComputeShaderRW, _meshletInstanceCountBuffer);
			commandList.PipelineBarrier(Renderer::PipelineBarrierType::ComputeWriteToIndirectArguments, _indirectDispatchArgumentBuffer);
			commandList.PipelineBarrier(Renderer::PipelineBarrierType::ComputeWriteToComputeShaderRead, _survivedInstanceDatas.GetBuffer());

			{
				shaderDesc.path = "Model/ExpandMeshlets.cs.hlsl";
				pipelineDesc.computeShader = _renderer->LoadShader(shaderDesc);

				Renderer::ComputePipelineID activePipeline = _renderer->CreatePipeline(pipelineDesc);
				commandList.BeginPipeline(activePipeline);

				commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::PER_DRAW, &_cullDescriptorSet, frameIndex);

				commandList.DispatchIndirect(_indirectDispatchArgumentBuffer, 4);

				commandList.EndPipeline(activePipeline);
			}
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

			commandList.PipelineBarrier(Renderer::PipelineBarrierType::ComputeWriteToIndirectArguments, _indirectDrawArgumentBuffer);
			commandList.PipelineBarrier(Renderer::PipelineBarrierType::ComputeWriteToVertexShaderRead, _culledIndices.GetBuffer());
			commandList.PipelineBarrier(Renderer::PipelineBarrierType::ComputeWriteToIndexBuffer, _culledIndices.GetBuffer());
			commandList.PipelineBarrier(Renderer::PipelineBarrierType::ComputeWriteToVertexShaderRead, _meshletInstanceBuffer);

			commandList.BeginPipeline(activePipeline);
			{
				commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::GLOBAL, &resources.globalDescriptorSet, frameIndex);
				commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::PER_DRAW, &_drawDescriptorSet, frameIndex);

				commandList.SetIndexBuffer(_culledIndices.GetBuffer(), Renderer::IndexFormat::UInt32);
				commandList.DrawIndexedIndirect(_indirectDrawArgumentBuffer, 0, 1);
			}
			commandList.EndPipeline(activePipeline);

			bool enableWireFrameMode = CVAR_ModelRendererWireFrameMode.Get() == 1;
			if (enableWireFrameMode)
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
			
					commandList.SetIndexBuffer(_culledIndices.GetBuffer(), Renderer::IndexFormat::UInt32);
					commandList.DrawIndexedIndirect(_indirectDrawArgumentBuffer, 0, 1);
				}
				commandList.EndPipeline(activePipeline);
			}
		});
}


u32 ModelRenderer::AddModel(u32 modelHash, const Model::Header& modelToLoad, std::shared_ptr<Bytebuffer>& buffer)
{
	u32 modelDataID = std::numeric_limits<u32>().max();

	_modelHashToModelID.WriteLock([&](robin_hood::unordered_map<u32, u32>& modelHashToModelID)
	{
		auto itr = modelHashToModelID.find(modelHash);
		if (itr == modelHashToModelID.end())
		{
			ModelData* modelDataPtr = nullptr;

			_modelData.WriteLock([&](std::vector<ModelData>& modelData)
			{
				modelDataID = modelData.size();
				modelDataPtr = &modelData.emplace_back();
			});

			size_t verticesBeforeAdd = 0;

			// Vertex Data
			{
				SafeVectorScopedWriteLock vertexPositionsLock(_vertexPositions);
				SafeVectorScopedWriteLock vertexNormalsLock(_vertexNormals);
				SafeVectorScopedWriteLock vertexUVsLock(_vertexUVs);

				std::vector<vec4>& vertexPositions = vertexPositionsLock.Get();
				std::vector<vec4>& vertexNormals = vertexNormalsLock.Get();
				std::vector<vec2>& vertexUVs = vertexUVsLock.Get();

				verticesBeforeAdd = vertexPositions.size();
				size_t verticesToAdd = modelToLoad.vertices.numElements;

				vertexPositions.resize(verticesBeforeAdd + verticesToAdd);
				vertexNormals.resize(verticesBeforeAdd + verticesToAdd);
				vertexUVs.resize(verticesBeforeAdd + verticesToAdd);

				for (u32 i = 0; i < verticesToAdd; i++)
				{
					const Model::Vertex* vertex = reinterpret_cast<Model::Vertex*>(&buffer->GetDataPointer()[modelToLoad.vertices.bufferOffset + (i * sizeof(Model::Vertex))]);

					vertexPositions[verticesBeforeAdd + i] = vec4(vertex->position, 1.0f);
					vertexNormals[verticesBeforeAdd + i] = vec4(vertex->normal, 1.0f);
					vertexUVs[verticesBeforeAdd + i] = vertex->uv;
				}
			}

			size_t indicesBeforeAdd = 0;
			_indices.WriteLock([&](std::vector<u32>& indices)
			{
				indicesBeforeAdd = indices.size();
				size_t indicesToAdd = modelToLoad.indices.numElements;

				indices.resize(indicesBeforeAdd + indicesToAdd);
				memcpy(&indices[indicesBeforeAdd], &buffer->GetDataPointer()[modelToLoad.indices.bufferOffset], indicesToAdd * sizeof(u32));
			});

			size_t meshletDataBeforeAdd = 0;
			size_t meshletDataToAdd = 0;
			_meshlets.WriteLock([&](std::vector<ModelRenderer::MeshletData>& meshletDatas)
			{
				meshletDataBeforeAdd = meshletDatas.size();
				meshletDataToAdd = 0;

				for (u32 i = 0; i < modelToLoad.meshes.numElements; i++)
				{
					ChunkPointer* meshChunkPointer = reinterpret_cast<ChunkPointer*>(&buffer->GetDataPointer()[sizeof(Model::Header) + (i * sizeof(ChunkPointer))]);
					meshletDataToAdd += meshChunkPointer->numElements;
				}

				meshletDatas.resize(meshletDataBeforeAdd + meshletDataToAdd);

				u32 meshletsAdded = 0;
				for (u32 i = 0; i < modelToLoad.meshes.numElements; i++)
				{
					ChunkPointer* meshChunkPointer = reinterpret_cast<ChunkPointer*>(&buffer->GetDataPointer()[sizeof(Model::Header) + (i * sizeof(ChunkPointer))]);

					for (u32 j = 0; j < meshChunkPointer->numElements; j++)
					{
						Model::Meshlet* modelMeshlet = reinterpret_cast<Model::Meshlet*>(&buffer->GetDataPointer()[meshChunkPointer->bufferOffset + (j * sizeof(Model::Meshlet))]);
						MeshletData& meshletData = meshletDatas[meshletDataBeforeAdd + meshletsAdded + j];

						meshletData.indexStart = modelMeshlet->indexStart;
						meshletData.indexCount = modelMeshlet->indexCount;
						assert(meshletData.indexCount % 3 == 0);
					}

					meshletsAdded += meshChunkPointer->numElements;
				}
			});

			modelDataPtr->meshletOffset = meshletDataBeforeAdd;
			modelDataPtr->meshletCount = meshletDataToAdd;
			modelDataPtr->indexOffset = indicesBeforeAdd;
			modelDataPtr->vertexOffset = verticesBeforeAdd;

			modelHashToModelID[modelHash] = modelDataID;

			DebugHandler::PrintSuccess("ModelRenderer : Added Model (Vertices : %u, Indices : %u, Meshes : %u, Meshlets : %u)", modelToLoad.vertices.numElements, modelToLoad.indices.numElements, modelToLoad.meshes.numElements, meshletDataToAdd);
		}
		else
		{
			modelDataID = itr->second;
		}
	});

	return modelDataID;
}

u32 ModelRenderer::AddInstance(u32 modelDataID, vec3 position, quat rotation, vec3 scale)
{
	u32 instanceID = std::numeric_limits<u32>().max();
	_instanceData.WriteLock([&](std::vector<InstanceData>& instanceDatas)
	{
		instanceID = instanceDatas.size();

		InstanceData& instanceData = instanceDatas.emplace_back();
		instanceData.modelDataID = modelDataID;
		instanceData.padding = 0;

		_survivedInstanceDatas.WriteLock([&](std::vector<SurvivedInstanceData>& survivedInstanceDatas)
		{
			survivedInstanceDatas.emplace_back();
		});

		_instanceMatrices.WriteLock([&](std::vector<mat4x4>& instanceMatrices)
		{
			mat4x4& matrix = instanceMatrices.emplace_back();

			mat4x4 rotationMatrix = glm::toMat4(rotation);
			mat4x4 scaleMatrix = glm::scale(mat4x4(1.0f), scale);
			matrix = glm::translate(mat4x4(1.0f), position) * rotationMatrix * scaleMatrix;
		});
	});

	return instanceID;
}
