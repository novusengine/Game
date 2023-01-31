#pragma once
#include <Base/Types.h>
#include <Base/Container/ConcurrentQueue.h>
#include <Base/Container/SafeUnorderedMap.h>

#include <type_safe/strong_typedef.hpp>

class TerrainRenderer;
class TerrainLoader
{
public:
	STRONG_TYPEDEF(Handle, u32);

	struct LoadDesc
	{
		ivec2 chunkGridPos;
		std::string path = "";
	};

	struct LoadHandle
	{
		Handle handle = Handle::Invalid();
		u32 instanceID = std::numeric_limits<u32>().max();
	};

private:
	struct LoadRequestInternal
	{
		u32 terrainHash = std::numeric_limits<u32>().max();
		std::string path = "";
		ivec2 chunkGridPos;
	};

	struct TerrainLoadStatus
	{
		enum class Status
		{
			InProgress,
			Completed,
			Failed
		};

		Status status = Status::InProgress;
		Handle activeHandle = Handle::Invalid(); // Only valid when Status is equal to InProgress
		u32 terrainDataID = 0;
	};

public:
	TerrainLoader(TerrainRenderer* terrainRenderer);
	
	void Update(f32 deltaTime);

	bool IsHandleActive(Handle handle);
	LoadHandle AddInstance(const LoadDesc& loadDesc);

	void Test();

private:
	void FreeHandle(Handle handle);

private:
	TerrainRenderer* _terrainRenderer = nullptr;

	moodycamel::ConcurrentQueue<LoadRequestInternal> _requests;

	SafeUnorderedMap<u32, TerrainLoadStatus> _terrainHashToTerrainLoadStatus;
	SafeUnorderedMap<u32, std::vector<u32>> _terrainHashToInstances;

	std::shared_mutex _handleMutex;
	Handle::type _maxHandleValue = 0;
	std::vector<Handle::type> _freedHandles;
	robin_hood::unordered_set<Handle::type> _activeHandles;
};