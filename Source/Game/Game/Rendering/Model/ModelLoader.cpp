#include "ModelLoader.h"
#include "ModelRenderer.h"

#include <Base/Memory/FileReader.h>

#include <filesystem>
namespace fs = std::filesystem;

ModelLoader::ModelLoader(ModelRenderer* modelRenderer) : _modelRenderer(modelRenderer), _requests()
{
	_freedHandles.reserve(64);
}

void ModelLoader::Update(f32 deltaTime)
{
	ModelLoader::LoadRequestInternal loadRequest;

	_modelHashToModelLoadStatus.WriteLock([&](robin_hood::unordered_map<u32, ModelLoadStatus>& modelHashToModelLoadStatus)
	{
		_modelHashToInstances.WriteLock([&](robin_hood::unordered_map<u32, std::vector<u32>>& modelHashToInstances)
		{
			while (_requests.try_dequeue(loadRequest))
			{
				//ModelLoadStatus& loadStatus = modelHashToModelLoadStatus[loadRequest.modelHash];
				//std::vector<u32>& instances = modelHashToInstances[loadRequest.modelHash];

				// Try Load Model
				{
					/*std::string modelPath = loadRequest.path;
					fs::path absoluteModelPath = fs::absolute(modelPath);

					std::string modelPathStr = absoluteModelPath.string();
					std::string modelFileNameStr = absoluteModelPath.filename().string();

					FileReader reader(modelPathStr, modelFileNameStr);
					if (reader.Open())
					{
						loadStatus.status = ModelLoadStatus::Status::Completed;

						size_t bufferSize = reader.Length();

						std::shared_ptr<Bytebuffer> modelBuffer = Bytebuffer::BorrowRuntime(bufferSize);
						reader.Read(modelBuffer.get(), bufferSize);

						u32 modelHash = StringUtils::fnv1a_32(modelPath.c_str(), modelPath.length());
						Model::Header* modelHeader = reinterpret_cast<Model::Header*>(modelBuffer->GetDataPointer());

						u32 modelDataID = _modelRenderer->AddModel(modelHash, *modelHeader, modelBuffer);

						Renderer::GPUVector<ModelRenderer::InstanceData>& instanceDataGPUVector = _modelRenderer->GetInstanceDatas();
						instanceDataGPUVector.WriteLock([&](std::vector<ModelRenderer::InstanceData>& instanceDatas)
						{
							for (u32 i = 0; i < instances.size(); i++)
							{
								u32 instanceID = instances[i];

								ModelRenderer::InstanceData& instanceData = instanceDatas[instanceID];
								instanceData.modelDataID = modelDataID;

								instanceDataGPUVector.SetDirtyElement(instanceID);
							}
						});

						instances.clear();
					}
					else
					{
						loadStatus.status = ModelLoadStatus::Status::Failed;

						Renderer::GPUVector<ModelRenderer::InstanceData>& instanceDataGPUVector = _modelRenderer->GetInstanceDatas();
						instanceDataGPUVector.WriteLock([&](std::vector<ModelRenderer::InstanceData>& instanceDatas)
						{
							for (u32 i = 0; i < instances.size(); i++)
							{
								u32 instanceID = instances[i];

								ModelRenderer::InstanceData& instanceData = instanceDatas[instanceID];
								instanceData.modelDataID = 1; // Checkboard White/Red Cube

								instanceDataGPUVector.SetDirtyElement(instanceID);
							}
						});
					}

					FreeHandle(loadStatus.activeHandle);
					loadStatus.activeHandle = Handle::Invalid();*/
				}
			}
		});
	});
}

bool ModelLoader::IsHandleActive(ModelLoader::Handle handle)
{
	u32 value = static_cast<u32>(handle);

	_handleMutex.lock_shared();
	bool result = _activeHandles.contains(value);
	_handleMutex.unlock_shared();

	return result;
}

ModelLoader::LoadHandle ModelLoader::AddInstance(const LoadDesc& loadDesc)
{
	LoadHandle loadHandle = { };

	u32 modelDataID = 0; // Checkboard White/Blue Cube
	u32 modelHash = StringUtils::fnv1a_32(loadDesc.path.c_str(), loadDesc.path.length());

	_modelHashToModelLoadStatus.WriteLock([&](robin_hood::unordered_map<u32, ModelLoadStatus>& modelHashToModelLoadStatus)
	{
		_modelHashToInstances.WriteLock([&](robin_hood::unordered_map<u32, std::vector<u32>>& modelHashToInstances)
		{
			auto itr = modelHashToModelLoadStatus.find(modelHash);

			ModelLoadStatus::Status status = ModelLoadStatus::Status::Failed;

			if (itr == modelHashToModelLoadStatus.end())
			{
				ModelLoadStatus modelLoadStatus;
				modelLoadStatus.status = ModelLoadStatus::Status::InProgress;
				status = modelLoadStatus.status;
				
				u32 handleValue = std::numeric_limits<u32>().max();

				// Get Handle
				{
					_handleMutex.lock();

					if (_freedHandles.size() > 0)
					{
						handleValue = _freedHandles.back();
						_freedHandles.pop_back();
					}
					else
					{
						handleValue = _maxHandleValue++;
					}

					_activeHandles.insert(handleValue);
					_handleMutex.unlock();

					modelLoadStatus.activeHandle = static_cast<Handle>(handleValue);
					loadHandle.handle = modelLoadStatus.activeHandle;
				}

				modelHashToModelLoadStatus[modelHash] = modelLoadStatus;

				LoadRequestInternal loadRequest;
				{
					loadRequest.modelHash = modelHash;
					loadRequest.path = loadDesc.path;
				}

				_requests.enqueue(loadRequest);
			}
			else
			{
				ModelLoadStatus& modelLoadStatus = itr->second;
				status = modelLoadStatus.status;

				if (modelLoadStatus.status == ModelLoadStatus::Status::InProgress)
				{
					loadHandle.handle = modelLoadStatus.activeHandle;
				}

				modelDataID = modelLoadStatus.modelDataID;
			}

			u32 instanceID = _modelRenderer->AddInstance(modelDataID, loadDesc.position, loadDesc.rotation, loadDesc.scale);
			loadHandle.instanceID = instanceID;

			if (status == ModelLoadStatus::Status::InProgress)
			{
				modelHashToInstances[modelHash].push_back(instanceID);
			}
		});
	});

	return loadHandle;
}

void ModelLoader::FreeHandle(ModelLoader::Handle handle)
{
	u32 value = static_cast<u32>(handle);

	_handleMutex.lock();

	if (_activeHandles.contains(value))
	{
		_activeHandles.erase(value);
		_freedHandles.push_back(value);
	}

	_handleMutex.unlock();
}
