#include "TerrainLoader.h"
#include "TerrainRenderer.h"

#include <Base/Memory/FileReader.h>

#include <FileFormat/Novus/Map/Map.h>
#include <FileFormat/Novus/Map/MapChunk.h>

#include <filesystem>
namespace fs = std::filesystem;

TerrainLoader::TerrainLoader(TerrainRenderer* terrainRenderer)
	: _terrainRenderer(terrainRenderer)
	, _requests()
{
	_freedHandles.reserve(64);
}

void TerrainLoader::Update(f32 deltaTime)
{
	LoadRequestInternal loadRequest;

	SafeUnorderedMapScopedWriteLock<u32, TerrainLoadStatus> terrainHashToTerrainLoadStatusLock(_terrainHashToTerrainLoadStatus);
	robin_hood::unordered_map<u32, TerrainLoadStatus>& terrainHashToTerrainLoadStatus = terrainHashToTerrainLoadStatusLock.Get();

	//SafeUnorderedMapScopedWriteLock<u32, std::vector<u32>> terrainHashToInstancesLock(_terrainHashToInstances);
	//robin_hood::unordered_map<u32, std::vector<u32>>& terrainHashToInstances = terrainHashToInstancesLock.Get();

	while (_requests.try_dequeue(loadRequest))
	{
		TerrainLoadStatus& loadStatus = terrainHashToTerrainLoadStatus[loadRequest.terrainHash];
		//std::vector<u32>& instances = terrainHashToInstances[loadRequest.terrainHash];

		std::string chunkPath = loadRequest.path;
		fs::path absoluteChunkPath = fs::absolute(chunkPath);

		std::string chunkPathStr = absoluteChunkPath.string();
		std::string chunkFileNameStr = absoluteChunkPath.filename().string();

		FileReader reader(chunkPathStr, chunkFileNameStr);
		if (reader.Open())
		{
			loadStatus.status = TerrainLoadStatus::Status::Completed;

			size_t bufferSize = reader.Length();

			std::shared_ptr<Bytebuffer> chunkBuffer = Bytebuffer::BorrowRuntime(bufferSize);
			reader.Read(chunkBuffer.get(), bufferSize);

			u32 chunkHash = StringUtils::fnv1a_32(chunkPath.c_str(), chunkPath.length());
			Map::Chunk* chunk = reinterpret_cast<Map::Chunk*>(chunkBuffer->GetDataPointer());

			/*u32 chunkDataID =*/ _terrainRenderer->AddChunk(chunkHash, chunk, loadRequest.chunkGridPos);

		}

	}
}

bool TerrainLoader::IsHandleActive(Handle handle)
{
	Handle::type value = static_cast<Handle::type>(handle);

	_handleMutex.lock_shared();
	bool result = _activeHandles.contains(value);
	_handleMutex.unlock_shared();

	return result;
}

TerrainLoader::LoadHandle TerrainLoader::AddInstance(const LoadDesc& loadDesc)
{
	LoadHandle loadHandle = { };

	u32 terrainDataID = 0; // Checkboard White/Blue Cube
	u32 terrainHash = StringUtils::fnv1a_32(loadDesc.path.c_str(), loadDesc.path.length());

	SafeUnorderedMapScopedWriteLock<u32, TerrainLoadStatus> terrainHashToTerrainLoadStatusLock(_terrainHashToTerrainLoadStatus);
	robin_hood::unordered_map<u32, TerrainLoadStatus>& terrainHashToTerrainLoadStatus = terrainHashToTerrainLoadStatusLock.Get();

	SafeUnorderedMapScopedWriteLock<u32, std::vector<u32>> terrainHashToInstancesLock(_terrainHashToInstances);
	robin_hood::unordered_map<u32, std::vector<u32>>& terrainHashToInstances = terrainHashToInstancesLock.Get();

	auto itr = terrainHashToTerrainLoadStatus.find(terrainHash);

	TerrainLoadStatus::Status status = TerrainLoadStatus::Status::Failed;

	if (itr == terrainHashToTerrainLoadStatus.end())
	{
		TerrainLoadStatus terrainLoadStatus;
		terrainLoadStatus.status = TerrainLoadStatus::Status::InProgress;
		status = terrainLoadStatus.status;

		Handle::type handleValue = std::numeric_limits<Handle::type>().max();

		// Get Handle
		{
			std::scoped_lock lock(_handleMutex);

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

			terrainLoadStatus.activeHandle = Handle(handleValue);
			loadHandle.handle = terrainLoadStatus.activeHandle;
		}

		terrainHashToTerrainLoadStatus[terrainHash] = terrainLoadStatus;

		LoadRequestInternal loadRequest;
		loadRequest.terrainHash = terrainHash;
		loadRequest.path = loadDesc.path;
		loadRequest.chunkGridPos = loadDesc.chunkGridPos;

		_requests.enqueue(loadRequest);
	}
	else
	{
		TerrainLoadStatus& terrainLoadStatus = itr->second;
		status = terrainLoadStatus.status;

		if (terrainLoadStatus.status == TerrainLoadStatus::Status::InProgress)
		{
			loadHandle.handle = terrainLoadStatus.activeHandle;
		}

		terrainDataID = terrainLoadStatus.terrainDataID;
	}


	if (terrainDataID == 0)
	{
	}
	u32 instanceID = 0;// _terrainRenderer->AddInstance(terrainDataID, loadDesc.chunkGridPos);
	loadHandle.instanceID = instanceID;

	if (status == TerrainLoadStatus::Status::InProgress)
	{
		terrainHashToInstances[terrainHash].push_back(instanceID);
	}

	return loadHandle;
}

void TerrainLoader::Test()
{
	/*
	{
		LoadDesc loadDesc;
		loadDesc.chunkGridPos = ivec2(32, 32);
		loadDesc.path = "Data/Terrain/Azeroth/Azeroth_32_32.nchunk";
		AddInstance(loadDesc);
	}
	{
		LoadDesc loadDesc;
		loadDesc.chunkGridPos = ivec2(33, 32);
		loadDesc.path = "Data/Terrain/Azeroth/Azeroth_33_32.nchunk";
		AddInstance(loadDesc);
	}*/

	
	for (u32 x = 0; x < 64; x++)
	{
		for (u32 y = 0; y < 64; y++)
		{
			LoadDesc loadDesc;
			loadDesc.chunkGridPos = ivec2(x, y);
			loadDesc.path = "Data/Map/Azeroth/Azeroth_" + std::to_string(x) + "_" + std::to_string(y) + ".chunk";
			AddInstance(loadDesc);
		}
	}

	/*std::string mapPath = "Terrain/Northrend/Northrend";

	u32 numUnoptimizedVertices = 0;
	u32 numOptimizedVertices = 0;

	constexpr u32 numVerticesPerChunk = Terrain::CHUNK_NUM_CELLS * Terrain::CELL_TOTAL_GRID_SIZE;

	static vec3* vertices = new vec3[numVerticesPerChunk];

	// Fill X and Z positions
	for (u32 i = 0; i < numVerticesPerChunk; i++)
	{


		vertices[i].x = 
	}

	for (u32 x = 0; x < Terrain::CHUNK_NUM_PER_MAP_STRIDE; x++)
	{
		for (u32 y = 0; y < Terrain::CHUNK_NUM_PER_MAP_STRIDE; y++)
		{
			std::string chunkPath = mapPath + "_" + std::to_string(x) + "_" + std::to_string(y) + ".nchunk";

			fs::path absoluteChunkPath = fs::absolute(chunkPath);

			std::string chunkPathStr = absoluteChunkPath.string();
			std::string chunkFileNameStr = absoluteChunkPath.filename().string();

			FileReader reader(chunkPathStr, chunkFileNameStr);
			if (reader.Open())
			{
				numUnoptimizedVertices += numVerticesPerChunk;

				size_t bufferSize = reader.Length();

				std::shared_ptr<Bytebuffer> chunkBuffer = Bytebuffer::BorrowRuntime(bufferSize);
				reader.Read(chunkBuffer.get(), bufferSize);

				u32 chunkHash = StringUtils::fnv1a_32(chunkPath.c_str(), chunkPath.length());
				Map::Chunk* chunk = reinterpret_cast<Map::Chunk*>(chunkBuffer->GetDataPointer());
			}
		}
	}*/
}

void TerrainLoader::FreeHandle(Handle handle)
{
	Handle::type value = static_cast<Handle::type>(handle);

	_handleMutex.lock();

	if (_activeHandles.contains(value))
	{
		_activeHandles.erase(value);
		_freedHandles.push_back(value);
	}

	_handleMutex.unlock();
}