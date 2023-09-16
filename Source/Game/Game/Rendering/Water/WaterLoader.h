#pragma once

#include <Base/Types.h>

namespace Map
{
	struct Chunk;
}

class WaterRenderer;

class WaterLoader
{
	static constexpr u32 MAX_LOADS_PER_FRAME = 65535;

private:
	struct LiquidInstance
	{
		u16 cellID;

		u8 typeID;
		u8 packedData;
		u8 packedOffset;
		u8 packedSize;
	};

	struct LoadRequestInternal
	{
		u16 chunkX;
		u16 chunkY;

		std::vector<LiquidInstance> instances;
		u8* vertexData = nullptr;
		u8* bitmapData = nullptr;
	};

public:
	WaterLoader(WaterRenderer* waterRenderer);

	void Init();
	void Clear();
	void Update(f32 deltaTime);

	void LoadFromChunk(u16 chunkX, u16 chunkY, Map::LiquidInfo* liquidInfo);

private:
	void LoadRequest(LoadRequestInternal& request, u32 bitmapOffset, u32 vertexDataOffset);

private:
	enki::TaskScheduler _scheduler;
	WaterRenderer* _waterRenderer = nullptr;

	LoadRequestInternal _workingRequests[MAX_LOADS_PER_FRAME];
	moodycamel::ConcurrentQueue<LoadRequestInternal> _requests;
};