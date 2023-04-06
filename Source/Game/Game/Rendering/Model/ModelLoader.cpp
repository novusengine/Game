#include "ModelLoader.h"
#include "ModelRenderer.h"
#include "Game/Application/EnttRegistries.h"
#include "Game/ECS/Singletons/JoltState.h"
#include "Game/Rendering/GameRenderer.h"
#include "Game/Rendering/Debug/DebugRenderer.h"
#include "Game/Util/ServiceLocator.h"

#include <Base/Memory/FileReader.h>
#include <Base/Util/StringUtils.h>

#include <FileFormat/Novus/Map/Map.h>
#include <FileFormat/Novus/Map/MapChunk.h>

#include <entt/entt.hpp>

#include <atomic>
#include <execution>
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

static const fs::path dataPath = fs::path("Data/");
static const fs::path complexModelPath = dataPath / "ComplexModel/";

ModelLoader::ModelLoader(ModelRenderer* modelRenderer)
	: _modelRenderer(modelRenderer)
	, _requests()
{
	_scheduler.Initialize();
	_workingRequests = new LoadRequestInternal[MAX_LOADS_PER_FRAME];
}

void ModelLoader::Init()
{
	DebugHandler::Print("ModelLoader : Scanning for models");

	static const fs::path fileExtension = ".complexmodel";

	// First recursively iterate the directory and find all paths
	std::vector<fs::path> paths;
	std::filesystem::recursive_directory_iterator dirpos{ complexModelPath };
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
				std::string cModelPath = relativePath./*make_preferred().*/string();
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
				Model::ComplexModel::ReadHeader(cModelBuffer, discoveredModel.modelHeader);

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
	_nameHashToLoadState.clear();
	_nameHashToModelID.clear();

	_instanceIDToModelID.clear();
	_modelIDToNameHash.clear();

	_modelRenderer->Clear();
}

void ModelLoader::Update(f32 deltaTime)
{
	// Count how many unique non-loaded request we have
	u32 numDequeued = static_cast<u32>(_requests.try_dequeue_bulk(_workingRequests, MAX_LOADS_PER_FRAME));

	if (numDequeued == 0)
		return;

	DebugHandler::Print("Loading {0}", numDequeued);

	u32 uniqueNonLoadedRequests = 0;
	ModelRenderer::ReserveInfo reserveInfo;
	reserveInfo.numInstances = numDequeued;

	robin_hood::unordered_map<u32, u32> loadCount(numDequeued);
	robin_hood::unordered_map<u32, u32> loadDrawcallCount(numDequeued);

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

		reserveInfo.numOpaqueDrawcalls += discoveredModel.modelHeader.numOpaqueRenderBatches;
		reserveInfo.numTransparentDrawcalls += discoveredModel.modelHeader.numTransparentRenderBatches;

		loadCount[nameHash]++;
		loadDrawcallCount[nameHash] += discoveredModel.modelHeader.numOpaqueRenderBatches;

		//DebugHandler::Print("ID: {0}, Namehash: {1}, DrawCalls: {2}", i, nameHash, discoveredModel.modelHeader.numOpaqueRenderBatches);

		if (!_nameHashToLoadState.contains(nameHash))
		{
			uniqueNonLoadedRequests++;
			_nameHashToLoadState[nameHash] = LoadState::Received;
			_nameHashToModelID[nameHash] = 0; // 0 should be a cube representing currently loading or something

			reserveInfo.numModels++;
			reserveInfo.numVertices += discoveredModel.modelHeader.numVertices;
			reserveInfo.numIndices += discoveredModel.modelHeader.numIndices;
			reserveInfo.numTextureUnits += discoveredModel.modelHeader.numTextureUnits;
		}
	}

	static int totalNumReserved = 0;
	totalNumReserved += reserveInfo.numOpaqueDrawcalls;

	//DebugHandler::Print("Total reserved: {0}", totalNumReserved);

	// Have ModelRenderer prepare all buffers for what we need to load
	_modelRenderer->Reserve(reserveInfo);

	u32 numTotalDrawcalls = 0;
	for (auto& it : loadCount)
	{
		DiscoveredModel& discoveredModel = _nameHashToDiscoveredModel[it.first];
		//DebugHandler::Print("NameHash: {0}, Instances: {1}, DrawCallsPer: {2}, DrawCalls: {3}", it.first, loadCount[it.first], discoveredModel.modelHeader.numOpaqueRenderBatches, loadDrawcallCount[it.first]);
		numTotalDrawcalls += loadDrawcallCount[it.first];

		if (numTotalDrawcalls > reserveInfo.numOpaqueDrawcalls)
		{
			volatile int asd = 123;
		}
	}


	for (u32 i = 0; i < numDequeued; i++) // To be parallelized
	{
		LoadRequestInternal& request = _workingRequests[i];

		LoadState loadState = _nameHashToLoadState[request.placement.nameHash];

		if (loadState == LoadState::Received)
		{
			_nameHashToLoadState[request.placement.nameHash] = LoadState::Loading;
			if (!LoadRequest(request))
				continue;

			/*DiscoveredModel& discoveredModel = _nameHashToDiscoveredModel[request.placement.nameHash];

			DebugHandler::Print("ID: {0}, Instances: {1}, DrawCallsPer: {2}, DrawCalls: {3}", i, loadCount[request.placement.nameHash], discoveredModel.modelHeader.numOpaqueRenderBatches, loadDrawcallCount[request.placement.nameHash]);
			numTotalDrawcalls += loadDrawcallCount[request.placement.nameHash];

			if (numTotalDrawcalls > reserveInfo.numOpaqueDrawcalls)
			{
				volatile int asd = 123;
			}*/
		}
		AddInstance(request);
	}
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
	return true;
}

void ModelLoader::AddInstance(const LoadRequestInternal& request)
{
	u32 modelID = _nameHashToModelID[request.placement.nameHash];
	u32 instanceID = _modelRenderer->AddInstance(modelID, request.placement);

	_instanceIDToModelID[instanceID] = modelID;
}