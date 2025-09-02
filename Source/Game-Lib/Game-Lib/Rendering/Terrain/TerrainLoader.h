#pragma once
#include "Game-Lib/Application/IOLoader.h"

#include <Base/Types.h>
#include <Base/Container/ConcurrentQueue.h>
#include <Base/Container/SafeUnorderedMap.h>

#include <enkiTS/TaskScheduler.h>
#include <robinhood/robinhood.h>
#include <type_safe/strong_typedef.hpp>

class ModelLoader;
class LiquidLoader;

namespace Map
{
    struct Chunk;
}

class TerrainRenderer;
struct TerrainReserveOffsets;
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
    public:
        LoadType loadType = LoadType::Full;
        std::string mapName = "";
        uvec2 chunkGridStartPos = uvec2(0, 0);
        uvec2 chunkGridEndPos = uvec2(0, 0);
    };

private:
    struct LoadRequestInternal
    {
    public:
        LoadType loadType = LoadType::Full;
        std::string mapName = "";
        uvec2 chunkGridStartPos = uvec2(0, 0);
        uvec2 chunkGridEndPos = uvec2(0, 0);
    };

    struct WorkRequest
    {
    public:
        u32 chunkID = std::numeric_limits<u16>().max();
        u32 chunkHash = std::numeric_limits<u32>().max();
        std::shared_ptr<Bytebuffer> data = nullptr;
    };

    struct ChunkInfo
    {
    public:
        Map::Chunk* chunk = nullptr;
        std::shared_ptr<Bytebuffer> data = nullptr;
    };

public:
    TerrainLoader(TerrainRenderer* terrainRenderer, ModelLoader* modelLoader, LiquidLoader* liquidLoader);
    
    void Clear();
    void Update(f32 deltaTime);

    void AddInstance(const LoadDesc& loadDesc);

    bool IsLoading() { return _numChunksToLoad != _numChunksLoaded; }
    f32 GetLoadingProgress() const;

    const std::string& GetCurrentMapInternalName() { return _currentMapInternalName; }

private:
    void LoadPartialMapRequest(const LoadRequestInternal& request);
    bool LoadFullMapRequest(const LoadRequestInternal& request);

    void IOLoadCallback(bool result, std::shared_ptr<Bytebuffer> buffer, const std::string& path, u64 userdata);

private:
    TerrainRenderer* _terrainRenderer = nullptr;
    std::string _currentMapInternalName = "";

    ModelLoader* _modelLoader = nullptr;
    LiquidLoader* _liquidLoader = nullptr;

    u32 _numChunksToLoad = 0;
    std::atomic<u32> _numChunksLoaded = 0;
    robin_hood::unordered_set<u32> _requestedChunkHashes;

    moodycamel::ConcurrentQueue<LoadRequestInternal> _requests;
    moodycamel::ConcurrentQueue<IOLoadRequest> _loadedChunkRequests;
    moodycamel::ConcurrentQueue<WorkRequest> _pendingWorkRequests;

    robin_hood::unordered_map<u32, u32> _chunkIDToLoadedID;
    robin_hood::unordered_map<u32, u32> _chunkIDToBodyID;
    robin_hood::unordered_map<u32, ChunkInfo> _chunkIDToChunkInfo;

    std::mutex _chunkLoadingMutex;
};
