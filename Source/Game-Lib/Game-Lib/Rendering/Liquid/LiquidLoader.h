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
    struct LoadRequestInternal
    {
    public:
        u16 chunkX;
        u16 chunkY;

        u32 numInstances;
        u32 numVertices;
        u32 numIndices;
        const Map::LiquidInfo* liquidInfo;
    };

public:
    LiquidLoader(LiquidRenderer* liquidRenderer);

    void Init();
    void Clear();
    void Update(f32 deltaTime);

    void LoadFromChunk(u16 chunkX, u16 chunkY, const Map::LiquidInfo* liquidInfo);

private:
    void LoadRequest(LoadRequestInternal& request, std::atomic<u32>& instanceOffset, std::atomic<u32>& vertexOffset, std::atomic<u32>& indexOffset);

private:
    LiquidRenderer* _liquidRenderer = nullptr;

    LoadRequestInternal _workingRequests[MAX_LOADS_PER_FRAME];
    moodycamel::ConcurrentQueue<LoadRequestInternal> _requests;
};