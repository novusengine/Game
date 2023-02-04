#include "TerrainLoader.h"
#include "TerrainRenderer.h"

#include <Base/Memory/FileReader.h>
#include <Base/Util/StringUtils.h>

#include <FileFormat/Novus/Map/Map.h>
#include <FileFormat/Novus/Map/MapChunk.h>

#include <execution>
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

TerrainLoader::TerrainLoader(TerrainRenderer* terrainRenderer)
	: _terrainRenderer(terrainRenderer)
	, _requests() 
{ 
	_scheduler.Initialize();
}

void TerrainLoader::Update(f32 deltaTime)
{
	LoadRequestInternal loadRequest;

	while (_requests.try_dequeue(loadRequest))
	{
		if (loadRequest.loadType == LoadType::Partial)
		{
			LoadPartialMapRequest(loadRequest);
		}
		else if (loadRequest.loadType == LoadType::Full)
		{
			LoadFullMapRequest(loadRequest);
		}
		else
		{
			DebugHandler::PrintFatal("TerrainLoader : Encountered LoadRequest with invalid LoadType");
		}
	}
}

void TerrainLoader::AddInstance(const LoadDesc& loadDesc)
{
	LoadRequestInternal loadRequest;
	loadRequest.loadType = loadDesc.loadType;
	loadRequest.mapName = loadDesc.mapName;
	loadRequest.chunkGridStartPos = loadDesc.chunkGridStartPos;
	loadRequest.chunkGridEndPos = loadDesc.chunkGridEndPos;

	_requests.enqueue(loadRequest);
}

void TerrainLoader::LoadPartialMapRequest(const LoadRequestInternal& request)
{
	std::string mapName = request.mapName;
	fs::path absoluteMapPath = fs::absolute("Data/Map/" + mapName);

	assert(request.loadType == LoadType::Partial);
	assert(request.mapName.size() > 0);
	assert(request.chunkGridStartPos.x <= request.chunkGridEndPos.x);
	assert(request.chunkGridStartPos.y <= request.chunkGridEndPos.y);

	u32 gridWidth = (request.chunkGridEndPos.x - request.chunkGridStartPos.x) + 1;
	u32 gridHeight = (request.chunkGridEndPos.y - request.chunkGridStartPos.y) + 1;

	u32 startChunkNum = request.chunkGridStartPos.x + (request.chunkGridStartPos.y * Terrain::CHUNK_NUM_PER_MAP_STRIDE);
	u32 chunksToLoad = gridHeight * gridWidth;

	std::atomic<u32> numExistingChunks = 0;
	enki::TaskSet countValidChunksTask(chunksToLoad, [&, startChunkNum](enki::TaskSetPartition range, uint32_t threadNum)
	{
		u32 localNumExistingChunks = 0;

		std::string chunkLocalPathStr = "";
		chunkLocalPathStr.reserve(128);

		for (u32 i = range.start; i < range.end; i++)
		{
			u32 chunkX = request.chunkGridStartPos.x + (i % gridWidth);
			u32 chunkY = request.chunkGridStartPos.y + (i / gridHeight);

			chunkLocalPathStr.clear();
			chunkLocalPathStr += mapName + "_" + std::to_string(chunkX) + "_" + std::to_string(chunkY) + ".chunk";

			fs::path chunkLocalPath = chunkLocalPathStr;
			fs::path chunkPath = absoluteMapPath / chunkLocalPath;

			if (!fs::exists(chunkPath))
				continue;

			localNumExistingChunks++;
		}

		numExistingChunks.fetch_add(localNumExistingChunks);
	});

	enki::TaskSet loadChunksTask(chunksToLoad, [&, startChunkNum](enki::TaskSetPartition range, uint32_t threadNum)
	{
		std::string chunkLocalPathStr = "";
		chunkLocalPathStr.reserve(128);

		for (u32 i = range.start; i < range.end; i++)
		{
			u32 chunkX = request.chunkGridStartPos.x + (i % gridWidth);
			u32 chunkY = request.chunkGridStartPos.y + (i / gridHeight);

			chunkLocalPathStr.clear();
			chunkLocalPathStr += mapName + "_" + std::to_string(chunkX) + "_" + std::to_string(chunkY) + ".chunk";

			fs::path chunkLocalPath = chunkLocalPathStr;
			fs::path chunkPath = absoluteMapPath / chunkLocalPath;

			if (!fs::exists(chunkPath))
				continue;

			std::string chunkPathStr = chunkPath.string();

			FileReader reader(chunkPathStr);
			if (reader.Open())
			{
				size_t bufferSize = reader.Length();

				std::shared_ptr<Bytebuffer> chunkBuffer = Bytebuffer::BorrowRuntime(bufferSize);
				reader.Read(chunkBuffer.get(), bufferSize);

				u32 chunkHash = StringUtils::fnv1a_32(chunkPathStr.c_str(), chunkPathStr.size());
				Map::Chunk* chunk = reinterpret_cast<Map::Chunk*>(chunkBuffer->GetDataPointer());

				u32 chunkDataID = _terrainRenderer->AddChunk(chunkHash, chunk, ivec2(chunkX, chunkY));
			}
		}
	});

	DebugHandler::Print("TerrainLoader : Started Preparing Chunk Loading");
	_scheduler.AddTaskSetToPipe(&countValidChunksTask);
	_scheduler.WaitforTask(&countValidChunksTask);
	DebugHandler::Print("TerrainLoader : Finished Preparing Chunk Loading");

	u32 numChunksToLoad = numExistingChunks.load();
	if (numChunksToLoad == 0)
	{
		DebugHandler::PrintError("TerrainLoader : Failed to prepare chunks for map '{0}'", request.mapName);
		return;
	}

	_terrainRenderer->ReserveChunks(numChunksToLoad);

	DebugHandler::Print("TerrainLoader : Started Chunk Loading");
	_scheduler.AddTaskSetToPipe(&loadChunksTask);
	_scheduler.WaitforTask(&loadChunksTask);
	DebugHandler::Print("TerrainLoader : Finished Chunk Loading");
}

void TerrainLoader::LoadFullMapRequest(const LoadRequestInternal& request)
{
	assert(request.loadType == LoadType::Full);
	assert(request.mapName.size() > 0);

	std::string mapName = request.mapName;
	fs::path absoluteMapPath = fs::absolute("Data/Map/" + mapName);

	if (!fs::is_directory(absoluteMapPath))
	{
		DebugHandler::PrintError("TerrainLoader : Failed to find '{0}' folder", absoluteMapPath.string());
		return;
	}

	std::vector<fs::path> paths;
	fs::directory_iterator dirpos{ absoluteMapPath };
	std::copy(begin(dirpos), end(dirpos), std::back_inserter(paths));

	u32 numPaths = static_cast<u32>(paths.size());
	std::atomic<u32> numExistingChunks = 0;

	enki::TaskSet countValidChunksTask(numPaths, [&](enki::TaskSetPartition range, uint32_t threadNum)
	{
		for (u32 i = range.start; i < range.end; i++)
		{
			const fs::path& path = paths[i];

			if (path.extension() != ".chunk")
				continue;

			numExistingChunks++;
		}
	});

	enki::TaskSet loadChunksTask(numPaths, [&](enki::TaskSetPartition range, uint32_t threadNum)
	{
		for (u32 i = range.start; i < range.end; i++)
		{
			const fs::path& path = paths[i];

			if (path.extension() != ".chunk")
				continue;

			std::string pathStr = path.string();

			std::vector<std::string> splitStrings = StringUtils::SplitString(pathStr, '_');
			u32 numSplitStrings = static_cast<u32>(splitStrings.size());

			u32 chunkX = std::stoi(splitStrings[numSplitStrings - 2]);
			u32 chunkY = std::stoi(splitStrings[numSplitStrings - 1].substr(0, 2));

			std::string chunkPathStr = path.string();

			FileReader reader(chunkPathStr);
			if (reader.Open())
			{
				size_t bufferSize = reader.Length();

				std::shared_ptr<Bytebuffer> chunkBuffer = Bytebuffer::BorrowRuntime(bufferSize);
				reader.Read(chunkBuffer.get(), bufferSize);

				u32 chunkHash = StringUtils::fnv1a_32(chunkPathStr.c_str(), chunkPathStr.size());
				Map::Chunk* chunk = reinterpret_cast<Map::Chunk*>(chunkBuffer->GetDataPointer());

				u32 chunkDataID = _terrainRenderer->AddChunk(chunkHash, chunk, ivec2(chunkX, chunkY));
			}
		}
	});

	DebugHandler::Print("TerrainLoader : Started Preparing Chunk Loading");
	_scheduler.AddTaskSetToPipe(&countValidChunksTask);
	_scheduler.WaitforTask(&countValidChunksTask);
	DebugHandler::Print("TerrainLoader : Finished Preparing Chunk Loading");

	u32 numChunksToLoad = numExistingChunks.load();
	if (numChunksToLoad == 0)
	{
		DebugHandler::PrintError("TerrainLoader : Failed to prepare chunks for map '{0}'", request.mapName);
		return;
	}

	_terrainRenderer->ClearChunks();
	_terrainRenderer->ReserveChunks(numChunksToLoad);

	DebugHandler::Print("TerrainLoader : Started Chunk Loading");
	_scheduler.AddTaskSetToPipe(&loadChunksTask);
	_scheduler.WaitforTask(&loadChunksTask);
	DebugHandler::Print("TerrainLoader : Finished Chunk Loading");
}
