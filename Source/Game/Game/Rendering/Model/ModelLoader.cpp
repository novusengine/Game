#include "ModelLoader.h"
#include "ModelRenderer.h"
#include "Game/Application/EnttRegistries.h"
#include "Game/ECS/Singletons/JoltState.h"
#include "Game/ECS/Components/Transform.h"
#include "Game/ECS/Components/Name.h"
#include "Game/ECS/Components/Model.h"
#include "Game/Rendering/GameRenderer.h"
#include "Game/Rendering/Debug/DebugRenderer.h"
#include "Game/Util/ServiceLocator.h"

#include <Base/Memory/FileReader.h>
#include <Base/Util/StringUtils.h>
#include <Base/CVarSystem/CVarSystem.h>

#include <FileFormat/Novus/Map/Map.h>
#include <FileFormat/Novus/Map/MapChunk.h>

#include <entt/entt.hpp>

#include <atomic>
#include <mutex>
#include <execution>
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

static const fs::path dataPath = fs::path("Data/");
static const fs::path complexModelPath = dataPath / "ComplexModel/";

AutoCVar_Int CVAR_ModelLoaderNumThreads("modelLoader.numThreads", "number of threads used for model loading, 0 = number of hardware threads", 0, CVarFlags::None);

ModelLoader::ModelLoader(ModelRenderer* modelRenderer)
	: _modelRenderer(modelRenderer)
	, _requests()
{
	i32 numThreads = CVAR_ModelLoaderNumThreads.Get();
	if (numThreads == 0)
	{
		_scheduler.Initialize();
	}
	else
	{
		_scheduler.Initialize(numThreads);
	}
}

void ModelLoader::Init()
{
	DebugHandler::Print("ModelLoader : Scanning for models");

	static const fs::path fileExtension = ".complexmodel";

	if (!fs::exists(complexModelPath))
	{
		fs::create_directories(complexModelPath);
	}

	// First recursively iterate the directory and find all paths
	std::vector<fs::path> paths;
	std::filesystem::recursive_directory_iterator dirpos { complexModelPath };
	std::copy(begin(dirpos), end(dirpos), std::back_inserter(paths));

	// Then create a multithreaded job to loop over the paths
	moodycamel::ConcurrentQueue<DiscoveredModel> discoveredModels;
	enki::TaskSet discoverModelsTask(static_cast<u32>(paths.size()), [&, paths](enki::TaskSetPartition range, u32 threadNum)
	{
		for (u32 i = range.start; i < range.end; i++)
		{
			const fs::path& path = paths[i];

			if (!path.has_extension() || path.extension().compare(fileExtension) != 0)
				continue;

			fs::path relativePath = fs::relative(path, complexModelPath);

			PRAGMA_MSVC_IGNORE_WARNING(4244);
			std::string cModelPath = relativePath.string();
			std::replace(cModelPath.begin(), cModelPath.end(), L'\\', L'/');

			FileReader cModelFile(path.string());
			if (!cModelFile.Open())
			{
				DebugHandler::PrintFatal("ModelLoader : Failed to open CModel file: {0}", path.string());
				continue;
			}

			// Load the first HEADER_SIZE of the file into memory
			size_t fileSize = cModelFile.Length();
			constexpr u32 HEADER_SIZE = sizeof(FileHeader) + sizeof(Model::ComplexModel::ModelHeader);

			if (fileSize < HEADER_SIZE)
			{
				DebugHandler::PrintError("ModelLoader : Tried to open CModel file ({0}) but it was smaller than sizeof(FileHeader) + sizeof(ModelHeader)", path.string());
				continue;
			}

			std::shared_ptr<Bytebuffer> cModelBuffer = Bytebuffer::Borrow<HEADER_SIZE>();

			cModelFile.Read(cModelBuffer.get(), HEADER_SIZE);
			cModelFile.Close();

			// Extract the ModelHeader from the file and store it as a DiscoveredModel
			DiscoveredModel discoveredModel;
			if (!Model::ComplexModel::ReadHeader(cModelBuffer, discoveredModel.modelHeader))
			{
				DebugHandler::PrintError("ModelLoader : Failed to read the ModelHeader for CModel file ({0})", path.string());
				continue;
			}

			discoveredModel.name = cModelPath;
			discoveredModel.nameHash = StringUtils::fnv1a_32(cModelPath.c_str(), cModelPath.length());

			discoveredModels.enqueue(discoveredModel);
		}
	});

	// Execute the multithreaded job
	_scheduler.AddTaskSetToPipe(&discoverModelsTask);
	_scheduler.WaitforTask(&discoverModelsTask);

	// And lastly move the data into the hashmap
	DiscoveredModel discoveredModel;
	while (discoveredModels.try_dequeue(discoveredModel))
	{
		_nameHashToDiscoveredModel[discoveredModel.nameHash] = discoveredModel;
	}

	DebugHandler::Print("Found {0} models", _nameHashToDiscoveredModel.size());
}

void ModelLoader::Clear()
{
	LoadRequestInternal dummyRequest;
	while (_requests.try_dequeue(dummyRequest))
	{
		// Just empty the queue
	}

	_nameHashToLoadState.clear();
	_nameHashToModelID.clear();

	for (auto& it : _nameHashToLoadingMutex)
	{
		if (it.second != nullptr)
		{
			delete it.second;
			it.second = nullptr;
		}
	}
	_nameHashToLoadingMutex.clear();

	_instanceIDToModelID.clear();
	_instanceIDToEntityID.clear();
	_modelIDToNameHash.clear();

	_modelRenderer->Clear();
}

void ModelLoader::Update(f32 deltaTime)
{
	// Count how many unique non-loaded request we have
	u32 numDequeued = static_cast<u32>(_requests.try_dequeue_bulk(&_workingRequests[0], MAX_LOADS_PER_FRAME));
	if (numDequeued == 0)
		return;

	ModelRenderer::ReserveInfo reserveInfo;

	for (u32 i = 0; i < numDequeued; i++)
	{
		LoadRequestInternal& request = _workingRequests[i];
		u32 nameHash = request.placement.nameHash;

		if (!_nameHashToDiscoveredModel.contains(nameHash))
		{
			DebugHandler::PrintError("ModelLoader : Tried to load model with hash ({0}) which wasn't discovered");
			continue;
		}

		DiscoveredModel& discoveredModel = _nameHashToDiscoveredModel[nameHash];
		bool isSupported = discoveredModel.modelHeader.numVertices > 0;

		// Only increment Instance Count & Drawcall Count if the model have vertices
		{
			reserveInfo.numInstances += 1 * isSupported;
			reserveInfo.numOpaqueDrawcalls += discoveredModel.modelHeader.numOpaqueRenderBatches * isSupported;
			reserveInfo.numTransparentDrawcalls += discoveredModel.modelHeader.numTransparentRenderBatches * isSupported;
		}

		if (!_nameHashToLoadState.contains(nameHash))
		{
			_nameHashToLoadState[nameHash] = LoadState::Received;
			_nameHashToModelID[nameHash] = 0; // 0 should be a cube representing currently loading or something
			_nameHashToLoadingMutex[nameHash] = new std::mutex();

			reserveInfo.numModels++;
			reserveInfo.numVertices += discoveredModel.modelHeader.numVertices * isSupported;
			reserveInfo.numIndices += discoveredModel.modelHeader.numIndices * isSupported;
			reserveInfo.numTextureUnits += discoveredModel.modelHeader.numTextureUnits * isSupported;
		}
	}

	// Have ModelRenderer prepare all buffers for what we need to load
	_modelRenderer->Reserve(reserveInfo);

	// Create entt entities
	entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
	_createdEntities.clear();
	_createdEntities.resize(reserveInfo.numInstances);
	registry->create(_createdEntities.begin(), _createdEntities.end());

	registry->insert<ECS::Components::DirtyTransform>(_createdEntities.begin(), _createdEntities.end());
	registry->insert<ECS::Components::Transform>(_createdEntities.begin(), _createdEntities.end());
	registry->insert<ECS::Components::Name>(_createdEntities.begin(), _createdEntities.end());
	registry->insert<ECS::Components::Model>(_createdEntities.begin(), _createdEntities.end());
	registry->insert<ECS::Components::AABB>(_createdEntities.begin(), _createdEntities.end());
	registry->insert<ECS::Components::WorldAABB>(_createdEntities.begin(), _createdEntities.end());

	std::atomic<u32> numCreatedInstances;
	enki::TaskSet loadModelsTask(reserveInfo.numInstances, [&](enki::TaskSetPartition range, u32 threadNum)
	{
		for (u32 i = range.start; i < range.end; i++)
		{
			LoadRequestInternal& request = _workingRequests[i];

			if (!_nameHashToDiscoveredModel.contains(request.placement.nameHash))
			{
				// Maybe we should add a warning that we tried to load a model that wasn't discovered? Or load an error cube or something?
				continue;
			}

			std::mutex* mutex = _nameHashToLoadingMutex[request.placement.nameHash];
			std::scoped_lock lock(*mutex);

			LoadState loadState = _nameHashToLoadState[request.placement.nameHash];

			if (loadState == LoadState::Failed)
				continue;

			if (loadState == LoadState::Received)
			{
				loadState = LoadState::Loading;
				_nameHashToLoadState[request.placement.nameHash] = LoadState::Loading;

				bool didLoad = LoadRequest(request);

				loadState = static_cast<LoadState>((LoadState::Loaded * didLoad) + (LoadState::Failed * !didLoad));;
				_nameHashToLoadState[request.placement.nameHash] = loadState;

				if (!didLoad)
					continue;
			}

			u32 index = numCreatedInstances.fetch_add(1);
			AddInstance(_createdEntities[index], request);
		}
	});

	// Execute the multithreaded job
	_scheduler.AddTaskSetToPipe(&loadModelsTask);
	_scheduler.WaitforTask(&loadModelsTask);

	// Destroy the entities we didn't use
	registry->destroy(_createdEntities.begin() + numCreatedInstances.load(), _createdEntities.end());

	// Fit the buffers to the data we loaded
	_modelRenderer->FitBuffersAfterLoad();
}

void ModelLoader::LoadPlacement(const Terrain::Placement& placement)
{
	LoadRequestInternal loadRequest;
	loadRequest.placement = placement;

	_requests.enqueue(loadRequest);
}

bool ModelLoader::GetModelIDFromInstanceID(u32 instanceID, u32& modelID)
{
	if (!_instanceIDToModelID.contains(instanceID))
		return false;

	modelID = _instanceIDToModelID[instanceID];
	return true;
}

bool ModelLoader::GetEntityIDFromInstanceID(u32 instanceID, entt::entity& entityID)
{
	if (!_instanceIDToEntityID.contains(instanceID))
		return false;

	entityID = _instanceIDToEntityID[instanceID];
	return true;
}

ModelLoader::DiscoveredModel& ModelLoader::GetDiscoveredModelFromModelID(u32 modelID)
{
	if (!_modelIDToNameHash.contains(modelID))
	{
		DebugHandler::PrintFatal("ModelLoader : Tried to access DiscoveredModel of invalid ModelID {0}", modelID);
	}

	u32 nameHash = _modelIDToNameHash[modelID];
	if (!_nameHashToDiscoveredModel.contains(nameHash))
	{
		DebugHandler::PrintFatal("ModelLoader : Tried to access DiscoveredModel of invalid NameHash {0}", nameHash);
	}

	return _nameHashToDiscoveredModel[nameHash];
}

bool ModelLoader::LoadRequest(const LoadRequestInternal& request)
{
	if (!_nameHashToDiscoveredModel.contains(request.placement.nameHash))
	{
		DebugHandler::PrintError("ModelLoader : Tried to load model nameHash ({0}) that doesn't exist", request.placement.nameHash);
		return false;
	}

	DiscoveredModel& discoveredModel = _nameHashToDiscoveredModel[request.placement.nameHash];

	fs::path path = complexModelPath / discoveredModel.name;
	FileReader cModelFile(path.string());
	if (!cModelFile.Open())
	{
		DebugHandler::PrintFatal("ModelLoader : Failed to open CModel file: {0}", path.string());
		return false;
	}

	// Load the file into memory
	size_t fileSize = cModelFile.Length();
	std::shared_ptr<Bytebuffer> cModelBuffer = Bytebuffer::BorrowRuntime(fileSize);

	cModelFile.Read(cModelBuffer.get(), fileSize);
	cModelFile.Close();

	// Extract the ComplexModel from the file
	Model::ComplexModel model;
	Model::ComplexModel::Read(cModelBuffer, model);

	if (model.modelHeader.numVertices == 0)
	{
		DebugHandler::PrintError("ModelLoader : Tried to load model ({0}) without any vertices", discoveredModel.name);
		return false;
	}


	u32 modelID = _modelRenderer->LoadModel(path.string(), model);
	_nameHashToModelID[request.placement.nameHash] = modelID;

	_modelIDToNameHash[modelID] = request.placement.nameHash;

	ECS::Components::AABB& aabb = _modelIDToAABB[modelID];
	aabb.centerPos = model.aabbCenter;
	aabb.extents = model.aabbExtents;

	return true;
}

void ModelLoader::AddInstance(entt::entity entityID, const LoadRequestInternal& request)
{
	entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;

	// Add components to the entity
	ECS::Components::DirtyTransform& dirtyTransfom = registry->get<ECS::Components::DirtyTransform>(entityID);

	ECS::Components::Transform& transform = registry->get<ECS::Components::Transform>(entityID);
	transform.position = request.placement.position;
	transform.rotation = request.placement.rotation;

	f32 scale = static_cast<f32>(request.placement.scale) / 1024.0f;
	transform.scale = vec3(scale, scale, scale);

	mat4x4 rotationMatrix = glm::toMat4(transform.rotation);
	mat4x4 scaleMatrix = glm::scale(mat4x4(1.0f), transform.scale);
	transform.matrix = glm::translate(mat4x4(1.0f), transform.position) * rotationMatrix * scaleMatrix;
	transform.isDirty = true;

	ECS::Components::Name& name = registry->get<ECS::Components::Name>(entityID);
	DiscoveredModel& discoveredModel = _nameHashToDiscoveredModel[request.placement.nameHash];
	name.name = StringUtils::GetFileNameFromPath(discoveredModel.name);
	name.fullName = discoveredModel.name;
	name.nameHash = discoveredModel.nameHash;

	u32 modelID = _nameHashToModelID[request.placement.nameHash];
	u32 instanceID = _modelRenderer->AddInstance(modelID, request.placement);

	ECS::Components::Model& model = registry->get<ECS::Components::Model>(entityID);
	model.modelID = modelID;
	model.instanceID = instanceID;

	const ECS::Components::AABB& modelAABB = _modelIDToAABB[modelID];

	ECS::Components::AABB& aabb = registry->get<ECS::Components::AABB>(entityID);
	aabb.centerPos = modelAABB.centerPos;
	aabb.extents = modelAABB.extents;

	std::scoped_lock lock(_instanceIDToModelIDMutex);
	_instanceIDToModelID[instanceID] = modelID;
	_instanceIDToEntityID[instanceID] = entityID;
}