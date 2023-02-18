#pragma once
#include <Base/Types.h>
#include <Base/Container/ConcurrentQueue.h>
#include <Base/Container/SafeUnorderedMap.h>

#include <enkiTS/TaskScheduler.h>
#include <robinhood/robinhood.h>
#include <type_safe/strong_typedef.hpp>

class TerrainRenderer;
class TerrainLoader
{
public:
	enum LoadType
	{
		Partial,
		Full
	};

	struct LoadDesc
	{
		LoadType loadType = LoadType::Full;
		std::string mapName = "";
		uvec2 chunkGridStartPos = uvec2(0, 0);
		uvec2 chunkGridEndPos = uvec2(0, 0);
	};

private:
	struct LoadRequestInternal
	{
		LoadType loadType = LoadType::Full;
		std::string mapName = "";
		uvec2 chunkGridStartPos = uvec2(0, 0);
		uvec2 chunkGridEndPos = uvec2(0, 0);
	};

public:
	TerrainLoader(TerrainRenderer* terrainRenderer);
	
	void Update(f32 deltaTime);

	void AddInstance(const LoadDesc& loadDesc);

private:
	void LoadPartialMapRequest(const LoadRequestInternal& request);
	void LoadFullMapRequest(const LoadRequestInternal& request);

	void PrepareForChunks(LoadType loadType, u32 numChunks);

private:
	enki::TaskScheduler _scheduler;
	TerrainRenderer* _terrainRenderer = nullptr;

	moodycamel::ConcurrentQueue<LoadRequestInternal> _requests;

	robin_hood::unordered_map<u32, u32> _chunkIDToLoadedID;
	robin_hood::unordered_map<u32, u32> _chunkIDToBodyID;
};