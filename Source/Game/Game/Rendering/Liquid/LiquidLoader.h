#pragma once

#include <Base/Types.h>
#include <Base/Container/ConcurrentQueue.h>

#include <enkiTS/TaskScheduler.h>

namespace Map
{
	struct Chunk;
	struct LiquidInfo;
}

class LiquidRenderer;

class LiquidLoader
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

		f32 height;
		u32 bitmapDataOffset;
		u32 vertexDataOffset;
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
	LiquidLoader(LiquidRenderer* liquidRenderer);

	void Init();
	void Clear();
	void Update(f32 deltaTime);

	void LoadFromChunk(u16 chunkX, u16 chunkY, Map::LiquidInfo* liquidInfo);

private:
	void LoadRequest(LoadRequestInternal& request);

private:
	LiquidRenderer* _liquidRenderer = nullptr;

	LoadRequestInternal _workingRequests[MAX_LOADS_PER_FRAME];
	moodycamel::ConcurrentQueue<LoadRequestInternal> _requests;
};