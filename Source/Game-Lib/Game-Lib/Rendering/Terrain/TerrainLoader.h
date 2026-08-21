#pragma once
#include <Base/Types.h>
#include <Base/Container/ConcurrentQueue.h>
#include <Base/Container/SafeUnorderedMap.h>

#include <Filesystem/Core/File.h>
#include <FileFormat/Shared.h>
#include <FileFormat/Novus/Map/Map.h>

#include <enkiTS/TaskScheduler.h>
#include <robinhood/robinhood.h>
#include <type_safe/strong_typedef.hpp>

#include <memory>
#include <string_view>
#include <vector>

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

    struct LoadedChunkView
    {
    public:
        Map::Chunk* chunk = nullptr;
        u32 chunkID = Terrain::CHUNK_INVALID_ID;
        u32 rendererChunkIndex = std::numeric_limits<u32>::max();
        u64 revision = 0;
    };

    struct ChunkLayoutState
    {
    public:
        std::vector<u32> occupiedChunkIDs;
        u64 generation = 0;
        bool headerDirty = false;
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
        // The PACT-backed chunk remains immutable. Editing uses a copy-on-write working
        // chunk so explicit save and future recovery can snapshot a stable revision.
        Map::Chunk* chunk = nullptr;
        std::shared_ptr<Map::Chunk> editableChunk;
        std::shared_ptr<Bytebuffer> buffer;
        std::shared_ptr<PACT::PactFileHandle> fileHandle;
        u64 revision = 0;
        bool replaceFileOnSave = false;
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

    u64 GetContentGeneration() const { return _contentGeneration.load(std::memory_order_relaxed); }
    bool IsMapHeaderDirty() const { return _mapHeaderDirty; }
    void GetLoadedChunks(std::vector<LoadedChunkView>& outChunks) const;
    bool GetEditableChunk(u32 chunkID, LoadedChunkView& outChunk);
    bool MarkChunkEdited(u32 chunkID);
    bool SaveEditableChunks(const std::vector<u32>& chunkIDs, const robin_hood::unordered_set<u32>& physicsDirtyChunkIDs, std::vector<u32>& outSavedChunkIDs);
    void GetChunkLayout(ChunkLayoutState& outState) const;
    bool AddChunk(u32 chunkID, bool& outCreated);
    bool RemoveChunk(u32 chunkID);
    bool ResetChunk(u32 chunkID);
    bool ReplaceGeneratedChunk(u32 chunkID, std::shared_ptr<Map::Chunk> chunk);
    bool SaveMapHeader();
    bool HasMapHeader(std::string_view mapInternalName) const;
    bool CreateEmptyMapHeader(std::string_view mapInternalName) const;

private:
    void LoadPartialMapRequest(const LoadRequestInternal& request);
    bool LoadFullMapRequest(const LoadRequestInternal& request);
    bool AttachChunk(u32 chunkID, bool replaceFileOnSave, std::shared_ptr<Bytebuffer> buffer, std::shared_ptr<PACT::PactFileHandle> fileHandle, std::shared_ptr<Map::Chunk> editableChunk);
    bool CreateChunkPhysics(u32 chunkID, std::shared_ptr<Bytebuffer>& buffer, Map::Chunk& chunk, u32& outBodyID);
    void RemoveChunkPhysics(u32 chunkID);
    std::string GetChunkPath(u32 chunkID) const;

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
    robin_hood::unordered_map<u32, u32> _unlinkedChunkRendererIndices;

    Map::MapHeader _mapHeader;
    bool _mapHeaderDirty = false;

    mutable std::mutex _chunkLoadingMutex;
    std::atomic<u64> _contentGeneration = 1;
};
