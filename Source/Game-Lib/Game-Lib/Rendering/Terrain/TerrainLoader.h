#pragma once
#include <Base/Types.h>
#include <Base/Container/ConcurrentQueue.h>
#include <Base/Container/SafeUnorderedMap.h>

#include <Filesystem/Core/File.h>

#include <enkiTS/TaskScheduler.h>
#include <robinhood/robinhood.h>
#include <type_safe/strong_typedef.hpp>

#include <memory>

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
        u64 fileHash = std::numeric_limits<u64>().max();

        std::shared_ptr<Bytebuffer> buffer;
        std::shared_ptr<PACT::PactFileHandle> fileHandle;
    };

    struct ChunkInfo
    {
    public:
        Map::Chunk* chunk = nullptr;
        std::shared_ptr<Bytebuffer> buffer;
        std::shared_ptr<PACT::PactFileHandle> fileHandle;
    };

public:
    TerrainLoader(TerrainRenderer* terrainRenderer, ModelLoader* modelLoader, LiquidLoader* liquidLoader);
    
    void Shutdown();
    void Clear();
    void Update(f32 deltaTime);

    void AddInstance(const LoadDesc& loadDesc);

    bool IsLoading() { return _numChunksToLoad != _numChunksLoaded; }
    f32 GetLoadingProgress() const;

    const std::string& GetCurrentMapInternalName() { return _currentMapInternalName; }

private:
    void LoadPartialMapRequest(const LoadRequestInternal& request);
    bool LoadFullMapRequest(const LoadRequestInternal& request);

private:
    TerrainRenderer* _terrainRenderer = nullptr;
    std::string _currentMapInternalName = "";

    ModelLoader* _modelLoader = nullptr;
    LiquidLoader* _liquidLoader = nullptr;

    u32 _numChunksToLoad = 0;
    std::atomic<u32> _numChunksLoaded = 0;
    std::atomic<u32> _numChunksFailed = 0;
    robin_hood::unordered_set<u32> _requestedChunkHashes;

    moodycamel::ConcurrentQueue<LoadRequestInternal> _requests;
    moodycamel::ConcurrentQueue<WorkRequest> _pendingWorkRequests;

    robin_hood::unordered_map<u32, u32> _chunkIDToLoadedID;
    robin_hood::unordered_map<u32, u32> _chunkIDToBodyID;
    robin_hood::unordered_map<u32, ChunkInfo> _chunkIDToChunkInfo;

    std::mutex _chunkLoadingMutex;
};
