#include "TerrainLoader.h"
#include "TerrainRenderer.h"

#include "Game-Lib/Application/EnttRegistries.h"
#include "Game-Lib/ECS/Components/AnimationData.h"
#include "Game-Lib/ECS/Components/Events.h"
#include "Game-Lib/ECS/Components//Model.h"
#include "Game-Lib/ECS/Singletons/AnimationSingleton.h"
#include "Game-Lib/ECS/Singletons/ActiveCamera.h"
#include "Game-Lib/ECS/Singletons/JoltState.h"
#include "Game-Lib/ECS/Systems/CharacterController.h"
#include "Game-Lib/ECS/Systems/Editor/EditorTools.h"
#include "Game-Lib/ECS/Util/EventUtil.h"
#include "Game-Lib/ECS/Util/Transforms.h"
#include "Game-Lib/Gameplay/MapLoader.h"
#include "Game-Lib/Rendering/Debug/DebugRenderer.h"
#include "Game-Lib/Rendering/Debug/JoltDebugRenderer.h"
#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Rendering/Model/ModelLoader.h"
#include "Game-Lib/Rendering/Liquid/LiquidLoader.h"
#include "Game-Lib/Util/AssetPath.h"
#include "Game-Lib/Util/AssetWriter.h"
#include "Game-Lib/Scripting/Handlers/MapHandler.h"
#include "Game-Lib/Scripting/Util/ZenithUtil.h"
#include "Game-Lib/Util/JoltStream.h"
#include "Game-Lib/Util/MapUtil.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Base/CVarSystem/CVarSystem.h>
#include <Base/Memory/FileReader.h>
#include <Base/Util/StringUtils.h>

#include <FileFormat/Novus/Map/Map.h>
#include <FileFormat/Novus/Map/MapChunk.h>

#include <Filesystem/PactStorage.h>

#include <MetaGen/EnumTraits.h>
#include <MetaGen/Game/Lua/Lua.h>

#include <Scripting/LuaManager.h>
#include <Scripting/Zenith.h>

#include <Jolt/Jolt.h>
#include <Jolt/Geometry/Triangle.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>

#include <entt/entt.hpp>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

namespace
{
    std::shared_ptr<Map::Chunk> CreateDefaultChunk()
    {
        auto chunk = std::make_shared<Map::Chunk>();
        std::memset(&chunk->cellsData, 0, sizeof(chunk->cellsData));
        chunk->heightHeader.gridMinHeight = 0.0f;
        chunk->heightHeader.gridMaxHeight = 0.0f;
        chunk->chunkAlphaMapTextureHash = Terrain::TEXTURE_ID_INVALID;

        for (u32 cellID = 0; cellID < Terrain::CHUNK_NUM_CELLS; cellID++)
        {
            chunk->cellsData.heightBounds[cellID] = vec2(0.0f);
            for (u32 vertexID = 0; vertexID < Terrain::CELL_TOTAL_GRID_SIZE; vertexID++)
            {
                chunk->cellsData.normals[cellID][vertexID][0] = 127;
                chunk->cellsData.normals[cellID][vertexID][1] = 254;
                chunk->cellsData.normals[cellID][vertexID][2] = 127;
                chunk->cellsData.colors[cellID][vertexID][0] = 255;
                chunk->cellsData.colors[cellID][vertexID][1] = 255;
                chunk->cellsData.colors[cellID][vertexID][2] = 255;
            }
        }

        return chunk;
    }

    bool BuildChunkPhysics(const Map::Chunk& chunk, std::vector<u8>& outPhysicsData)
    {
        JPH::VertexList vertices;
        JPH::IndexedTriangleList triangles;
        vertices.reserve(Terrain::CHUNK_NUM_CELLS * Terrain::CELL_TOTAL_GRID_SIZE);
        triangles.reserve(Terrain::CHUNK_NUM_CELLS * Terrain::CELL_NUM_TRIANGLES);

        for (u32 cellID = 0; cellID < Terrain::CHUNK_NUM_CELLS; cellID++)
        {
            for (u32 vertexID = 0; vertexID < Terrain::CELL_TOTAL_GRID_SIZE; vertexID++)
            {
                const vec2 position = Util::Map::GetCellVertexPosition(cellID, vertexID);
                vertices.push_back({ position.x, chunk.cellsData.heightField[cellID][vertexID], position.y });
            }

            const u32 cellVertexOffset = cellID * Terrain::CELL_TOTAL_GRID_SIZE;
            for (u32 triangleID = 0; triangleID < Terrain::CELL_NUM_TRIANGLES; triangleID++)
            {
                const u32 patchID = triangleID / 4;
                if ((chunk.cellsData.holes[cellID] & (1ull << patchID)) != 0)
                    continue;

                const u32 patchRow = patchID / 8;
                const u32 patchColumn = patchID % 8;
                const u32 patchVertices[5] = {
                    patchColumn + patchRow * Terrain::CELL_GRID_ROW_SIZE,
                    patchColumn + patchRow * Terrain::CELL_GRID_ROW_SIZE + 1,
                    patchColumn + patchRow * Terrain::CELL_GRID_ROW_SIZE + Terrain::CELL_GRID_ROW_SIZE,
                    patchColumn + patchRow * Terrain::CELL_GRID_ROW_SIZE + Terrain::CELL_GRID_ROW_SIZE + 1,
                    patchColumn + patchRow * Terrain::CELL_GRID_ROW_SIZE + Terrain::CELL_OUTER_GRID_STRIDE
                };
                const u32 triangleWithinPatch = triangleID % 4;
                const uvec2 componentOffsets(triangleWithinPatch > 1, triangleWithinPatch == 0 || triangleWithinPatch == 3);
                const u32 vertexID1 = cellVertexOffset + patchVertices[4];
                const u32 vertexID2 = cellVertexOffset + patchVertices[componentOffsets.x * 2 + componentOffsets.y];
                const u32 vertexID3 = cellVertexOffset + patchVertices[(!componentOffsets.y) * 2 + componentOffsets.x];
                triangles.push_back({ vertexID3, vertexID2, vertexID1 });
            }
        }

        JPH::MeshShapeSettings shapeSettings(vertices, triangles);
        JPH::ShapeSettings::ShapeResult shapeResult = shapeSettings.Create();
        if (shapeResult.HasError())
            return false;

        JPH::Shape::ShapeToIDMap shapeMap;
        JPH::Shape::MaterialToIDMap materialMap;
        std::shared_ptr<Bytebuffer> physicsBuffer = Bytebuffer::BorrowRuntime(16 * 1024 * 1024);
        JoltStreamOut stream(physicsBuffer.get());
        shapeResult.Get()->SaveWithChildren(stream, shapeMap, materialMap);
        if (stream.IsFailed() || physicsBuffer->writtenData == 0)
            return false;

        outPhysicsData.assign(physicsBuffer->GetDataPointer(), physicsBuffer->GetDataPointer() + physicsBuffer->writtenData);
        return true;
    }

    struct ChunkPayload
    {
    public:
        std::vector<Terrain::Placement> placements;
        Map::LiquidInfo liquidInfo;
        std::vector<u8> physicsData;
    };

    template <typename T>
    bool ReadChunkPayloadElements(const std::shared_ptr<Bytebuffer>& buffer, u64 offset, u32 count, std::vector<T>& outElements)
    {
        if (count == 0)
        {
            outElements.clear();
            return true;
        }

        const size_t byteCount = static_cast<size_t>(count) * sizeof(T);
        if (!buffer || offset > buffer->writtenData || byteCount > buffer->writtenData - static_cast<size_t>(offset))
            return false;

        outElements.resize(count);
        std::memcpy(outElements.data(), buffer->GetDataPointer() + offset, byteCount);
        return true;
    }

    bool ReadChunkPayload(const Map::Chunk& chunk, const std::shared_ptr<Bytebuffer>& buffer, ChunkPayload& outPayload)
    {
        if (!ReadChunkPayloadElements(buffer, chunk.placementHeader.dataOffset, chunk.placementHeader.numPlacements, outPayload.placements))
            return false;

        u64 liquidOffset = chunk.liquidHeader.dataOffset;
        if (!ReadChunkPayloadElements(buffer, liquidOffset, chunk.liquidHeader.numHeaders, outPayload.liquidInfo.headers))
            return false;
        liquidOffset += static_cast<u64>(chunk.liquidHeader.numHeaders) * sizeof(Map::CellLiquidHeader);

        if (!ReadChunkPayloadElements(buffer, liquidOffset, chunk.liquidHeader.numInstances, outPayload.liquidInfo.instances))
            return false;
        liquidOffset += static_cast<u64>(chunk.liquidHeader.numInstances) * sizeof(Map::CellLiquidInstance);

        if (!ReadChunkPayloadElements(buffer, liquidOffset, chunk.liquidHeader.numAttributes, outPayload.liquidInfo.attributes))
            return false;
        liquidOffset += static_cast<u64>(chunk.liquidHeader.numAttributes) * sizeof(Map::CellLiquidAttributes);

        if (!ReadChunkPayloadElements(buffer, liquidOffset, chunk.liquidHeader.numBitmapBytes, outPayload.liquidInfo.bitmapData))
            return false;
        liquidOffset += chunk.liquidHeader.numBitmapBytes;

        if (!ReadChunkPayloadElements(buffer, liquidOffset, chunk.liquidHeader.numVertexBytes, outPayload.liquidInfo.vertexData))
            return false;

        return ReadChunkPayloadElements(buffer, chunk.physicsHeader.dataOffset, chunk.physicsHeader.numBytes, outPayload.physicsData);
    }

    bool SerializeChunk(Map::Chunk& chunk, std::shared_ptr<Bytebuffer>& outBuffer)
    {
        std::vector<u8> physicsData;
        if (!BuildChunkPhysics(chunk, physicsData))
            return false;

        std::shared_ptr<Bytebuffer> workingBuffer = Bytebuffer::BorrowRuntime(16 * 1024 * 1024);
        const std::vector<Terrain::Placement> placements;
        const Map::LiquidInfo liquidInfo;
        if (!chunk.Save(workingBuffer, placements, liquidInfo, physicsData))
            return false;

        outBuffer = std::make_shared<Bytebuffer>(nullptr, workingBuffer->writtenData);
        std::memcpy(outBuffer->GetDataPointer(), workingBuffer->GetDataPointer(), workingBuffer->writtenData);
        outBuffer->writtenData = workingBuffer->writtenData;
        return true;
    }

    bool SerializeEditedChunkWithPhysics(Map::Chunk& chunk, std::shared_ptr<Bytebuffer> sourceBuffer, std::shared_ptr<Bytebuffer>& outBuffer)
    {
        if (!sourceBuffer || sourceBuffer->writtenData < sizeof(Map::Chunk))
            return false;

        Map::Chunk sourceChunk;
        if (!Map::Chunk::Read(sourceBuffer, sourceChunk))
            return false;

        ChunkPayload payload;
        if (!ReadChunkPayload(sourceChunk, sourceBuffer, payload))
            return false;

        if (!BuildChunkPhysics(chunk, payload.physicsData))
            return false;

        outBuffer = Bytebuffer::BorrowRuntime(16 * 1024 * 1024);
        return chunk.Save(outBuffer, payload.placements, payload.liquidInfo, payload.physicsData);
    }
}

AutoCVar_Int CVAR_TerrainChunkLoadsPerFrame(CVarCategory::Client | CVarCategory::Rendering, "terrainChunkLoadsPerFrame", "maximum terrain chunks prepared and committed per frame", 32, CVarFlags::None);

TerrainLoader::TerrainLoader(TerrainRenderer* terrainRenderer, ModelLoader* modelLoader, LiquidLoader* liquidLoader)
    : _terrainRenderer(terrainRenderer)
    , _modelLoader(modelLoader)
    , _liquidLoader(liquidLoader)
    , _requests()
    , _pendingWorkRequests()
{
    ZoneScoped;

    _chunkIDToLoadedID.reserve(4096);
    _chunkIDToBodyID.reserve(4096);
    _chunkIDToChunkInfo.reserve(4096);
    _unlinkedChunkRendererIndices.reserve(4096);
}

static void NotifyCurrentMapChanged()
{
    Scripting::LuaManager* luaManager = ServiceLocator::GetLuaManager();
    Scripting::Zenith* zenith = Scripting::Util::Zenith::GetGlobal();
    if (!luaManager || !zenith)
        return;
    auto* handler = luaManager->GetLuaHandler<Scripting::Map::MapHandler>(static_cast<u16>(MetaGen::Game::Lua::LuaHandlerTypeEnum::Map));
    if (handler)
        handler->OnCurrentMapChanged(zenith);
}

void TerrainLoader::Shutdown()
{
    ZoneScopedN("TerrainLoader::Shutdown");

    // Chunk tasks are frame-local and waited in Update. Clear drains queued work and
    // releases the loaded chunk records that retain PACT file handles.
    Clear();
}

void TerrainLoader::Clear()
{
    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
    auto& joltState = registry->ctx().get<ECS::Singletons::JoltState>();

    if (!_currentMapInternalName.empty())
        joltState.LogPhysicsTelemetrySummary("Before TerrainLoader::Clear");
    
    LoadRequestInternal loadRequest;
    while (_requests.try_dequeue(loadRequest)) { }

    u32 numBodyIDs = static_cast<u32>(_chunkIDToBodyID.size());
    if (numBodyIDs > 0)
    {
        JPH::BodyInterface& bodyInterface = joltState.physicsSystem.GetBodyInterface();
        std::vector<JPH::BodyID> bodyIDs;
        bodyIDs.reserve(numBodyIDs);

        for (auto& pair : _chunkIDToBodyID)
        {
            JPH::BodyID id = static_cast<JPH::BodyID>(pair.second);
            bodyIDs.push_back(id);

        }

        bodyInterface.RemoveBodies(&bodyIDs[0], numBodyIDs);
        bodyInterface.DestroyBodies(&bodyIDs[0], numBodyIDs);
    }
    
    _numChunksToLoad = 0;
    _numChunksLoaded = 0;
    _numChunksFailed = 0;
    _requestedChunkHashes.clear();
    
    WorkRequest workRequest;
    while (_pendingWorkRequests.try_dequeue(workRequest)) { }
    
    _chunkIDToLoadedID.clear();
    _chunkIDToBodyID.clear();
    _unlinkedChunkRendererIndices.clear();
    _mapHeader = {};
    _mapHeaderDirty = false;

    {
        std::scoped_lock lock(_chunkLoadingMutex);
        _chunkIDToChunkInfo.clear();
        _contentGeneration.fetch_add(1, std::memory_order_relaxed);
    }
    
    ServiceLocator::GetGameRenderer()->GetModelLoader()->Clear();
    ServiceLocator::GetGameRenderer()->GetLiquidLoader()->Clear();
    ServiceLocator::GetGameRenderer()->GetJoltDebugRenderer()->Clear();
    _terrainRenderer->Clear();
    
    // Clear any editor selection -- the unloaded map's selected entity no longer exists.
    ECS::Systems::Editor::EditorTools::SetSelectedEntity(*ServiceLocator::GetEnttRegistries()->gameRegistry, entt::null);

    const bool mapInternalNameChanged = !_currentMapInternalName.empty();
    _currentMapInternalName.clear();
    if (mapInternalNameChanged)
        NotifyCurrentMapChanged();

    auto view = registry->view<ECS::Components::Model>();
    view.each([&](ECS::Components::Model& model)
    {
        model.modelID = std::numeric_limits<u32>().max();
        model.instanceID = std::numeric_limits<u32>().max();
    });
    
    registry->clear<ECS::Components::AnimationData>();
    registry->clear<ECS::Components::AnimationInitData>();
    registry->clear<ECS::Components::AnimationStaticInstance>();
    
    auto& animationSingleton = registry->ctx().get<ECS::Singletons::AnimationSingleton>();
    
    for (auto& pair : animationSingleton.staticModelIDToEntity)
    {
        entt::entity entity = pair.second;
        if (registry->valid(entity))
        {
            registry->destroy(entity);
        }
    }
    
    animationSingleton.staticModelIDToEntity.clear();
}

void TerrainLoader::Update(f32 deltaTime)
{
    ZoneScopedN("TerrainLoader::Update");

    LoadRequestInternal loadRequest;

    size_t numRequests = _requests.size_approx();
    if (numRequests > 0)
    {
        ZoneScopedN("LoadRequest");

        LoadRequestInternal loadRequest;
        while (_requests.try_dequeue(loadRequest)) {}

        if (loadRequest.loadType == LoadType::Partial)
        {
            // TODO : This needs to be implemented
            //LoadPartialMapRequest(loadRequest);
        }
        else if (loadRequest.loadType == LoadType::Full)
        {
            if (LoadFullMapRequest(loadRequest))
            {
                _modelLoader->SetTerrainLoading(true);
            }
        }
        else
        {
            NC_LOG_CRITICAL("TerrainLoader : Encountered LoadRequest with invalid LoadType");
        }
    }

    u32 numChunksToLoad = _numChunksToLoad;
    u32 numChunksLoadedBefore = _numChunksLoaded;

    u32 numPendingRequests = static_cast<u32>(_pendingWorkRequests.size_approx());
    TracyPlot("Terrain Chunk Load Backlog", static_cast<i64>(numPendingRequests));
    if (numPendingRequests > 0)
    {
        ZoneScopedN("PendingWork");
        enki::TaskScheduler* taskScheduler = ServiceLocator::GetTaskScheduler();
        i32 physicsEnabled = *CVarSystem::Get()->GetIntCVar(CVarCategory::Client | CVarCategory::Physics, "enabled"_h);

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& joltState = registry->ctx().get<ECS::Singletons::JoltState>();

        const u32 configuredChunkLoadsPerFrame = static_cast<u32>(std::max(1, CVAR_TerrainChunkLoadsPerFrame.Get()));
        u32 maxChunkLoadsThisTick = glm::min(numPendingRequests, configuredChunkLoadsPerFrame);
        TracyPlot("Terrain Chunks Loaded This Frame", static_cast<i64>(maxChunkLoadsThisTick));

        TerrainReserveOffsets reserveOffsets;
        _terrainRenderer->AllocateChunks(maxChunkLoadsThisTick, reserveOffsets);
        _chunkIDToLoadedID.reserve(_chunkIDToLoadedID.size() + maxChunkLoadsThisTick);
        _chunkIDToBodyID.reserve(_chunkIDToBodyID.size() + maxChunkLoadsThisTick);

        enki::TaskSet loadChunksTask(maxChunkLoadsThisTick, [this, &reserveOffsets](enki::TaskSetPartition range, uint32_t threadNum)
        {
            ZoneScopedN("Load Chunk Task");
            u32 numProcessedLoads = 0;
            u32 numFailedLoads = 0;

            WorkRequest workRequest;
            for (u32 i = range.start; i < range.end; i++)
            {
                if (!_pendingWorkRequests.try_dequeue(workRequest))
                    break;

                ZoneScopedN("Load Chunk Worker");
                numProcessedLoads++;

                if (!_requestedChunkHashes.contains(workRequest.chunkHash))
                    continue;

                workRequest.fileHandle = std::make_shared<PACT::PactFileHandle>();
                if (ServiceLocator::GetPactStorage()->ReadFile(workRequest.fileHash, *workRequest.fileHandle) != PACT::PactReadResult::Success)
                {
                    NC_LOG_ERROR("TerrainLoader : Failed to read chunk asset {0}", workRequest.fileHash);
                    numFailedLoads++;
                    continue;
                }

                workRequest.buffer = std::make_shared<Bytebuffer>(const_cast<void*>(workRequest.fileHandle->GetData()), workRequest.fileHandle->GetSize());
                workRequest.buffer->writtenData = workRequest.fileHandle->GetSize();

                u32 chunkX = workRequest.chunkID % Terrain::CHUNK_NUM_PER_MAP_STRIDE;
                u32 chunkY = workRequest.chunkID / Terrain::CHUNK_NUM_PER_MAP_STRIDE;

                Map::Chunk* chunk = reinterpret_cast<Map::Chunk*>(const_cast<void*>(workRequest.fileHandle->GetData()));
                if (chunk->header.type != FileHeader::Type::MapChunk)
                {
                    NC_LOG_ERROR("TerrainLoader : Invalid Chunk Header");
                    numFailedLoads++;
                    continue;
                }

                if (chunk->header.version != Map::Chunk::CURRENT_VERSION)
                {
                    NC_LOG_ERROR("TerrainLoader : Invalid Chunk Version. Expected {0} but got {1}", Map::Chunk::CURRENT_VERSION, chunk->header.version);
                    numFailedLoads++;
                    continue;
                }

                u32 physicsBodyID = JPH::BodyID::cInvalidBodyID;
                if (!CreateChunkPhysics(workRequest.chunkID, workRequest.buffer, *chunk, physicsBodyID))
                {
                    NC_LOG_ERROR("TerrainLoader : Failed to restore physics for chunk {0}", workRequest.chunkID);
                    numFailedLoads++;
                    continue;
                }

                // Load into Terrain Renderer
                {
                    ZoneScopedN("Add Chunk To Renderer");
                    u32 chunkDataIndex = reserveOffsets.chunkDataStartOffset + i;
                    u32 cellDataStartIndex = reserveOffsets.cellDataStartOffset + (i * Terrain::CHUNK_NUM_CELLS);
                    u32 vertexDataStartIndex = reserveOffsets.vertexDataStartOffset + (i * Terrain::CHUNK_NUM_CELLS * Terrain::CELL_NUM_VERTICES);

                    u32 chunkDataID = _terrainRenderer->AddChunk(workRequest.chunkHash, chunk, ivec2(chunkX, chunkY), chunkDataIndex, cellDataStartIndex, vertexDataStartIndex);
                    
                    {
                        std::scoped_lock lock(_chunkLoadingMutex);
                        _chunkIDToChunkInfo[workRequest.chunkID] = { .chunk = chunk, .buffer = workRequest.buffer, .fileHandle = std::move(workRequest.fileHandle) };
                        _chunkIDToLoadedID[workRequest.chunkID] = chunkDataID;
                        _contentGeneration.fetch_add(1, std::memory_order_relaxed);

                        if (physicsBodyID != JPH::BodyID::cInvalidBodyID)
                            _chunkIDToBodyID[workRequest.chunkID] = physicsBodyID;
                    }

                    {
                        ZoneScopedN("Load Chunk Placements");

                        u32 numPlacements = chunk->placementHeader.numPlacements;
                        for (u32 placementIndex = 0; placementIndex < numPlacements; placementIndex++)
                        {
                            auto* placement = chunk->placementHeader.GetPlacement(workRequest.buffer, placementIndex);
                            _modelLoader->LoadPlacement(*placement);
                        }
                    }

                    // Load Liquid
                    {
                        ZoneScopedN("Process Chunk Liquid");

                        u32 numLiquidHeaders = static_cast<u32>(chunk->liquidHeader.numHeaders);
                        if (numLiquidHeaders != 0 && numLiquidHeaders != 256)
                        {
                            NC_LOG_CRITICAL("LiquidHeader should always contain either 0 or 256 liquid headers, but it contained {0} liquid headers", numLiquidHeaders);
                        }

                        if (numLiquidHeaders == 256)
                        {
                            ZoneScopedN("Load Chunk Liquid");
                            _liquidLoader->LoadFromChunk(chunkX, chunkY, workRequest.buffer, chunk->liquidHeader);
                        }
                    }
                }

            }

            _numChunksLoaded += numProcessedLoads;
            _numChunksFailed += numFailedLoads;
        });

        taskScheduler->AddTaskSetToPipe(&loadChunksTask);
        taskScheduler->WaitforTask(&loadChunksTask);

        u32 numChunksLoadedAfter = _numChunksLoaded;
        bool finishedLoadThisFrame = numChunksLoadedBefore < numChunksToLoad && numChunksLoadedAfter >= numChunksToLoad;
        if (finishedLoadThisFrame)
        {
            i32 physicsOptimizeBP = *CVarSystem::Get()->GetIntCVar(CVarCategory::Client | CVarCategory::Physics, "optimizeBP"_h);
            if (physicsEnabled && physicsOptimizeBP)
            {
                joltState.physicsSystem.OptimizeBroadPhase();
            }

            const u32 numFailedChunks = _numChunksFailed;
            NC_LOG_INFO("TerrainLoader : Loaded {0}/{1} chunks ({2} failed)", numChunksLoadedAfter - numFailedChunks, numChunksLoadedAfter, numFailedChunks);
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

f32 TerrainLoader::GetLoadingProgress() const
{
    if (_numChunksToLoad == 0)
        return 1.0f;

    f32 progress = static_cast<f32>(_numChunksLoaded) / static_cast<f32>(_numChunksToLoad);
    return progress;
}

void TerrainLoader::GetLoadedChunks(std::vector<LoadedChunkView>& outChunks) const
{
    std::scoped_lock lock(_chunkLoadingMutex);
    outChunks.clear();
    outChunks.reserve(_chunkIDToChunkInfo.size());

    for (const auto& [chunkID, chunkInfo] : _chunkIDToChunkInfo)
    {
        auto loadedIDItr = _chunkIDToLoadedID.find(chunkID);
        if (loadedIDItr == _chunkIDToLoadedID.end())
            continue;

        outChunks.push_back({ .chunk = chunkInfo.editableChunk ? chunkInfo.editableChunk.get() : chunkInfo.chunk, .chunkID = chunkID, .rendererChunkIndex = loadedIDItr->second, .revision = chunkInfo.revision });
    }
}

bool TerrainLoader::GetEditableChunk(u32 chunkID, LoadedChunkView& outChunk)
{
    std::scoped_lock lock(_chunkLoadingMutex);
    auto chunkInfoItr = _chunkIDToChunkInfo.find(chunkID);
    auto loadedIDItr = _chunkIDToLoadedID.find(chunkID);
    if (chunkInfoItr == _chunkIDToChunkInfo.end() || loadedIDItr == _chunkIDToLoadedID.end() || !chunkInfoItr->second.chunk)
        return false;

    ChunkInfo& chunkInfo = chunkInfoItr->second;
    if (!chunkInfo.editableChunk)
    {
        chunkInfo.editableChunk = std::make_shared<Map::Chunk>(*chunkInfo.chunk);
        _contentGeneration.fetch_add(1, std::memory_order_relaxed);
    }

    outChunk = {
        .chunk = chunkInfo.editableChunk.get(),
        .chunkID = chunkID,
        .rendererChunkIndex = loadedIDItr->second,
        .revision = chunkInfo.revision
    };
    return true;
}

bool TerrainLoader::MarkChunkEdited(u32 chunkID)
{
    std::scoped_lock lock(_chunkLoadingMutex);
    auto itr = _chunkIDToChunkInfo.find(chunkID);
    if (itr == _chunkIDToChunkInfo.end() || !itr->second.editableChunk)
        return false;

    itr->second.revision++;
    _contentGeneration.fetch_add(1, std::memory_order_relaxed);
    return true;
}

bool TerrainLoader::SaveEditableChunks(const std::vector<u32>& chunkIDs, const robin_hood::unordered_set<u32>& physicsDirtyChunkIDs, std::vector<u32>& outSavedChunkIDs)
{
    struct ChunkSaveSnapshot
    {
    public:
        u32 chunkID = Terrain::CHUNK_INVALID_ID;
        u64 revision = 0;
        std::string virtualPath;
        Map::Chunk editedChunk;
        std::vector<u8> sourceBytes;
        std::vector<u8> bytes;
        bool replaceFile = false;
        bool rebuildPhysics = false;
    };

    outSavedChunkIDs.clear();
    if (chunkIDs.empty())
        return true;

    std::vector<ChunkSaveSnapshot> snapshots;
    snapshots.reserve(chunkIDs.size());
    {
        std::scoped_lock lock(_chunkLoadingMutex);
        if (_currentMapInternalName.empty())
            return false;

        for (u32 chunkID : chunkIDs)
        {
            auto itr = _chunkIDToChunkInfo.find(chunkID);
            if (itr == _chunkIDToChunkInfo.end())
                continue;

            const ChunkInfo& chunkInfo = itr->second;
            if (!chunkInfo.editableChunk)
                continue;

            const u32 chunkX = chunkID % Terrain::CHUNK_NUM_PER_MAP_STRIDE;
            const u32 chunkY = chunkID / Terrain::CHUNK_NUM_PER_MAP_STRIDE;
            ChunkSaveSnapshot& snapshot = snapshots.emplace_back();
            snapshot.chunkID = chunkID;
            snapshot.revision = chunkInfo.revision;
            snapshot.virtualPath = Util::AssetPath::Map(_currentMapInternalName + "/" + _currentMapInternalName + "_" + std::to_string(chunkX) + "_" + std::to_string(chunkY) + ".chunk");
            snapshot.editedChunk = *chunkInfo.editableChunk;
            snapshot.replaceFile = chunkInfo.replaceFileOnSave;
            snapshot.rebuildPhysics = physicsDirtyChunkIDs.contains(chunkID);
            if (snapshot.replaceFile && chunkInfo.buffer)
            {
                snapshot.sourceBytes.resize(chunkInfo.buffer->writtenData);
                std::memcpy(snapshot.sourceBytes.data(), chunkInfo.buffer->GetDataPointer(), snapshot.sourceBytes.size());
            }
        }
    }

    Util::AssetWriter* assetWriter = ServiceLocator::GetAssetWriter();
    PACT::PactStorage* pactStorage = ServiceLocator::GetPactStorage();
    if (!assetWriter || !pactStorage)
        return false;

    bool savedAllChunks = snapshots.size() == chunkIDs.size();
    for (ChunkSaveSnapshot& snapshot : snapshots)
    {
        if (!snapshot.sourceBytes.empty())
            continue;

        PACT::PactFileHandle sourceFile;
        if (pactStorage->ReadFile(snapshot.virtualPath, sourceFile) != PACT::PactReadResult::Success || sourceFile.GetSize() < sizeof(Map::Chunk))
        {
            savedAllChunks = false;
            continue;
        }

        snapshot.sourceBytes.resize(sourceFile.GetSize());
        std::memcpy(snapshot.sourceBytes.data(), sourceFile.GetData(), snapshot.sourceBytes.size());
    }

    for (ChunkSaveSnapshot& snapshot : snapshots)
    {
        if (snapshot.sourceBytes.empty())
            continue;

        if (!snapshot.rebuildPhysics)
        {
            snapshot.bytes = std::move(snapshot.sourceBytes);
            std::memcpy(snapshot.bytes.data(), &snapshot.editedChunk, sizeof(Map::Chunk));
            continue;
        }

        std::shared_ptr<Bytebuffer> sourceBuffer = std::make_shared<Bytebuffer>(snapshot.sourceBytes.data(), snapshot.sourceBytes.size());
        sourceBuffer->writtenData = snapshot.sourceBytes.size();
        std::shared_ptr<Bytebuffer> serializedBuffer;
        if (!SerializeEditedChunkWithPhysics(snapshot.editedChunk, sourceBuffer, serializedBuffer))
        {
            NC_LOG_ERROR("TerrainLoader : Failed to serialize edited chunk {0} with rebuilt physics", snapshot.chunkID);
            savedAllChunks = false;
            continue;
        }

        snapshot.bytes.resize(serializedBuffer->writtenData);
        std::memcpy(snapshot.bytes.data(), serializedBuffer->GetDataPointer(), snapshot.bytes.size());
    }

    for (ChunkSaveSnapshot& snapshot : snapshots)
    {
        if (snapshot.bytes.empty())
            continue;

        if (!assetWriter->WriteBytes(snapshot.virtualPath, snapshot.bytes, Util::AssetWriteTarget::PactOverlay))
        {
            savedAllChunks = false;
            continue;
        }

        std::scoped_lock lock(_chunkLoadingMutex);
        auto itr = _chunkIDToChunkInfo.find(snapshot.chunkID);
        if (itr == _chunkIDToChunkInfo.end() || itr->second.revision != snapshot.revision)
        {
            savedAllChunks = false;
            continue;
        }

        if (snapshot.rebuildPhysics)
        {
            std::shared_ptr<Bytebuffer> serializedBuffer = std::make_shared<Bytebuffer>(snapshot.bytes.data(), snapshot.bytes.size());
            serializedBuffer->writtenData = snapshot.bytes.size();
            u32 bodyID = JPH::BodyID::cInvalidBodyID;
            if (!CreateChunkPhysics(snapshot.chunkID, serializedBuffer, snapshot.editedChunk, bodyID))
            {
                NC_LOG_ERROR("TerrainLoader : Failed to replace physics for saved chunk {0}", snapshot.chunkID);
                savedAllChunks = false;
                continue;
            }

            RemoveChunkPhysics(snapshot.chunkID);
            if (bodyID != JPH::BodyID::cInvalidBodyID)
                _chunkIDToBodyID[snapshot.chunkID] = bodyID;
        }

        outSavedChunkIDs.push_back(snapshot.chunkID);
        itr->second.editableChunk->placementHeader = snapshot.editedChunk.placementHeader;
        itr->second.editableChunk->liquidHeader = snapshot.editedChunk.liquidHeader;
        itr->second.editableChunk->physicsHeader = snapshot.editedChunk.physicsHeader;
        itr->second.replaceFileOnSave = false;
    }

    return savedAllChunks;
}

void TerrainLoader::GetChunkLayout(ChunkLayoutState& outState) const
{
    std::scoped_lock lock(_chunkLoadingMutex);
    outState.occupiedChunkIDs.clear();
    outState.occupiedChunkIDs.reserve(_mapHeader.chunkHashes.size());

    robin_hood::unordered_set<u64> occupiedHashes(_mapHeader.chunkHashes.begin(), _mapHeader.chunkHashes.end());
    const u32 chunkCount = Terrain::CHUNK_NUM_PER_MAP_STRIDE * Terrain::CHUNK_NUM_PER_MAP_STRIDE;
    for (u32 chunkID = 0; chunkID < chunkCount; chunkID++)
    {
        if (occupiedHashes.contains(Util::AssetPath::Hash(GetChunkPath(chunkID))))
            outState.occupiedChunkIDs.push_back(chunkID);
    }

    outState.generation = _contentGeneration.load(std::memory_order_relaxed);
    outState.headerDirty = _mapHeaderDirty;
}

std::string TerrainLoader::GetChunkPath(u32 chunkID) const
{
    const u32 chunkX = chunkID % Terrain::CHUNK_NUM_PER_MAP_STRIDE;
    const u32 chunkY = chunkID / Terrain::CHUNK_NUM_PER_MAP_STRIDE;
    return Util::AssetPath::Map(_currentMapInternalName + "/" + _currentMapInternalName + "_" + std::to_string(chunkX) + "_" + std::to_string(chunkY) + Map::CHUNK_FILE_EXTENSION);
}

bool TerrainLoader::CreateChunkPhysics(u32 chunkID, std::shared_ptr<Bytebuffer>& buffer, Map::Chunk& chunk, u32& outBodyID)
{
    outBodyID = JPH::BodyID::cInvalidBodyID;
    const i32 physicsEnabled = *CVarSystem::Get()->GetIntCVar(CVarCategory::Client | CVarCategory::Physics, "enabled"_h);
    if (!physicsEnabled || chunk.physicsHeader.numBytes == 0)
        return true;

    if (chunk.physicsHeader.numBytes > buffer->writtenData || chunk.physicsHeader.dataOffset > buffer->writtenData - chunk.physicsHeader.numBytes)
        return false;

    Bytebuffer physicsBuffer(chunk.physicsHeader.GetPhysicsData(buffer), chunk.physicsHeader.numBytes);
    physicsBuffer.SkipWrite(chunk.physicsHeader.numBytes);
    JoltStreamIn stream(&physicsBuffer);
    JPH::Shape::IDToShapeMap shapeMap;
    JPH::Shape::IDToMaterialMap materialMap;
    JPH::ShapeSettings::ShapeResult shapeResult = JPH::Shape::sRestoreWithChildren(stream, shapeMap, materialMap);
    if (shapeResult.HasError())
        return false;

    auto& joltState = ServiceLocator::GetEnttRegistries()->gameRegistry->ctx().get<ECS::Singletons::JoltState>();
    JPH::BodyInterface& bodyInterface = joltState.physicsSystem.GetBodyInterface();
    const vec2 chunkPosition = Util::Map::GetChunkPosition(chunkID);
    JPH::BodyCreationSettings bodySettings(shapeResult.Get(), JPH::RVec3(chunkPosition.x, 0.0f, chunkPosition.y), JPH::Quat::sIdentity(), JPH::EMotionType::Static, Jolt::Layers::NON_MOVING);
    JPH::Body* body = bodyInterface.CreateBody(bodySettings);
    joltState.RecordBodyCreate(ECS::Singletons::JoltBodyTelemetrySource::TerrainChunk, body != nullptr);
    if (!body)
        return false;

    body->SetUserData(Jolt::PhysicsBodyUserData::Pack(entt::null, Jolt::PhysicsSurfaceType::Terrain, Jolt::PhysicsBodyFlags::CanSupport | Jolt::PhysicsBodyFlags::CanSnapTo));
    bodyInterface.AddBody(body->GetID(), JPH::EActivation::Activate);
    outBodyID = body->GetID().GetIndexAndSequenceNumber();
    return true;
}

void TerrainLoader::RemoveChunkPhysics(u32 chunkID)
{
    auto bodyItr = _chunkIDToBodyID.find(chunkID);
    if (bodyItr == _chunkIDToBodyID.end())
        return;

    auto& joltState = ServiceLocator::GetEnttRegistries()->gameRegistry->ctx().get<ECS::Singletons::JoltState>();
    JPH::BodyInterface& bodyInterface = joltState.physicsSystem.GetBodyInterface();
    const JPH::BodyID bodyID = static_cast<JPH::BodyID>(bodyItr->second);
    bodyInterface.RemoveBody(bodyID);
    bodyInterface.DestroyBody(bodyID);
    _chunkIDToBodyID.erase(bodyItr);
}

bool TerrainLoader::AttachChunk(u32 chunkID, bool replaceFileOnSave, std::shared_ptr<Bytebuffer> buffer, std::shared_ptr<PACT::PactFileHandle> fileHandle, std::shared_ptr<Map::Chunk> editableChunk)
{
    if (!buffer || buffer->writtenData < sizeof(Map::Chunk))
        return false;

    Map::Chunk* chunk = editableChunk ? editableChunk.get() : reinterpret_cast<Map::Chunk*>(buffer->GetDataPointer());
    if (!chunk || chunk->header.type != FileHeader::Type::MapChunk || chunk->header.version != Map::Chunk::CURRENT_VERSION)
        return false;

    u32 bodyID = JPH::BodyID::cInvalidBodyID;
    if (!CreateChunkPhysics(chunkID, buffer, *chunk, bodyID))
        return false;

    const u32 chunkX = chunkID % Terrain::CHUNK_NUM_PER_MAP_STRIDE;
    const u32 chunkY = chunkID / Terrain::CHUNK_NUM_PER_MAP_STRIDE;
    const std::string chunkPath = GetChunkPath(chunkID);
    const u32 chunkHash = StringUtils::fnv1a_32(chunkPath.c_str(), chunkPath.size());
    u32 rendererChunkIndex = 0;
    auto unlinkedItr = _unlinkedChunkRendererIndices.find(chunkID);
    if (unlinkedItr != _unlinkedChunkRendererIndices.end())
    {
        rendererChunkIndex = unlinkedItr->second;
        if (!_terrainRenderer->ReplaceChunk(rendererChunkIndex, chunkHash, *chunk, ivec2(chunkX, chunkY)))
            return false;
        _unlinkedChunkRendererIndices.erase(unlinkedItr);
    }
    else
    {
        rendererChunkIndex = _terrainRenderer->AddChunk(chunkHash, chunk, ivec2(chunkX, chunkY));
    }

    ChunkInfo chunkInfo = {
        .chunk = chunk,
        .editableChunk = std::move(editableChunk),
        .buffer = std::move(buffer),
        .fileHandle = std::move(fileHandle),
        .replaceFileOnSave = replaceFileOnSave
    };
    _chunkIDToChunkInfo[chunkID] = std::move(chunkInfo);
    _chunkIDToLoadedID[chunkID] = rendererChunkIndex;
    if (bodyID != JPH::BodyID::cInvalidBodyID)
        _chunkIDToBodyID[chunkID] = bodyID;
    return true;
}

bool TerrainLoader::AddChunk(u32 chunkID, bool& outCreated)
{
    outCreated = false;
    const u32 chunkCount = Terrain::CHUNK_NUM_PER_MAP_STRIDE * Terrain::CHUNK_NUM_PER_MAP_STRIDE;
    if (_currentMapInternalName.empty() || IsLoading() || chunkID >= chunkCount || _chunkIDToChunkInfo.contains(chunkID))
        return false;

    const std::string chunkPath = GetChunkPath(chunkID);
    const u64 fileHash = Util::AssetPath::Hash(chunkPath);
    std::shared_ptr<PACT::PactFileHandle> fileHandle = std::make_shared<PACT::PactFileHandle>();
    std::shared_ptr<Bytebuffer> buffer;
    std::shared_ptr<Map::Chunk> editableChunk;
    if (ServiceLocator::GetPactStorage()->ReadFile(chunkPath, *fileHandle) == PACT::PactReadResult::Success)
    {
        buffer = std::make_shared<Bytebuffer>(const_cast<void*>(fileHandle->GetData()), fileHandle->GetSize());
        buffer->writtenData = fileHandle->GetSize();
    }
    else
    {
        fileHandle.reset();
        editableChunk = CreateDefaultChunk();
        if (!SerializeChunk(*editableChunk, buffer))
            return false;
        outCreated = true;
    }

    const bool replaceFileOnSave = !fileHandle;
    if (!AttachChunk(chunkID, replaceFileOnSave, std::move(buffer), std::move(fileHandle), std::move(editableChunk)))
        return false;

    if (std::find(_mapHeader.chunkHashes.begin(), _mapHeader.chunkHashes.end(), fileHash) == _mapHeader.chunkHashes.end())
        _mapHeader.chunkHashes.push_back(fileHash);
    _mapHeaderDirty = true;
    _contentGeneration.fetch_add(1, std::memory_order_relaxed);
    return true;
}

bool TerrainLoader::RemoveChunk(u32 chunkID)
{
    const u64 fileHash = Util::AssetPath::Hash(GetChunkPath(chunkID));
    if (std::find(_mapHeader.chunkHashes.begin(), _mapHeader.chunkHashes.end(), fileHash) == _mapHeader.chunkHashes.end())
        return false;

    auto loadedItr = _chunkIDToLoadedID.find(chunkID);
    if (IsLoading())
        return false;

    if (loadedItr != _chunkIDToLoadedID.end())
    {
        if (!_terrainRenderer->HideChunk(loadedItr->second))
            return false;

        RemoveChunkPhysics(chunkID);
        _unlinkedChunkRendererIndices[chunkID] = loadedItr->second;
        _chunkIDToLoadedID.erase(loadedItr);
        _chunkIDToChunkInfo.erase(chunkID);

        // Chunk placements and liquids currently have no per-chunk ownership handle. They
        // intentionally remain live until the map is unloaded and must not be loaded again
        // if this chunk is linked again during the same map session.
    }

    std::erase(_mapHeader.chunkHashes, fileHash);
    _mapHeaderDirty = true;
    _contentGeneration.fetch_add(1, std::memory_order_relaxed);
    return true;
}

bool TerrainLoader::ResetChunk(u32 chunkID)
{
    auto loadedItr = _chunkIDToLoadedID.find(chunkID);
    if (IsLoading() || loadedItr == _chunkIDToLoadedID.end())
        return false;

    std::shared_ptr<Map::Chunk> chunk = CreateDefaultChunk();
    std::shared_ptr<Bytebuffer> buffer;
    if (!SerializeChunk(*chunk, buffer))
        return false;

    RemoveChunkPhysics(chunkID);
    const u32 rendererChunkIndex = loadedItr->second;
    const u32 chunkX = chunkID % Terrain::CHUNK_NUM_PER_MAP_STRIDE;
    const u32 chunkY = chunkID / Terrain::CHUNK_NUM_PER_MAP_STRIDE;
    const std::string chunkPath = GetChunkPath(chunkID);
    const u32 chunkHash = StringUtils::fnv1a_32(chunkPath.c_str(), chunkPath.size());
    if (!_terrainRenderer->ReplaceChunk(rendererChunkIndex, chunkHash, *chunk, ivec2(chunkX, chunkY)))
        return false;

    u32 bodyID = JPH::BodyID::cInvalidBodyID;
    if (!CreateChunkPhysics(chunkID, buffer, *chunk, bodyID))
        return false;

    _chunkIDToChunkInfo[chunkID] = {
        .chunk = chunk.get(),
        .editableChunk = std::move(chunk),
        .buffer = std::move(buffer),
        .revision = 1,
        .replaceFileOnSave = true
    };
    if (bodyID != JPH::BodyID::cInvalidBodyID)
        _chunkIDToBodyID[chunkID] = bodyID;
    _contentGeneration.fetch_add(1, std::memory_order_relaxed);
    return true;
}

bool TerrainLoader::ReplaceGeneratedChunk(u32 chunkID, std::shared_ptr<Map::Chunk> chunk)
{
    const u32 chunkCount = Terrain::CHUNK_NUM_PER_MAP_STRIDE * Terrain::CHUNK_NUM_PER_MAP_STRIDE;
    if (_currentMapInternalName.empty() || IsLoading() || !chunk || chunkID >= chunkCount)
        return false;

    std::shared_ptr<Bytebuffer> buffer;
    if (!SerializeChunk(*chunk, buffer))
        return false;

    auto loadedItr = _chunkIDToLoadedID.find(chunkID);
    if (loadedItr == _chunkIDToLoadedID.end())
    {
        if (!AttachChunk(chunkID, true, std::move(buffer), nullptr, std::move(chunk)))
            return false;
    }
    else
    {
        RemoveChunkPhysics(chunkID);
        const u32 rendererChunkIndex = loadedItr->second;
        const u32 chunkX = chunkID % Terrain::CHUNK_NUM_PER_MAP_STRIDE;
        const u32 chunkY = chunkID / Terrain::CHUNK_NUM_PER_MAP_STRIDE;
        const std::string chunkPath = GetChunkPath(chunkID);
        const u32 chunkHash = StringUtils::fnv1a_32(chunkPath.c_str(), chunkPath.size());
        if (!_terrainRenderer->ReplaceChunk(rendererChunkIndex, chunkHash, *chunk, ivec2(chunkX, chunkY)))
            return false;

        u32 bodyID = JPH::BodyID::cInvalidBodyID;
        if (!CreateChunkPhysics(chunkID, buffer, *chunk, bodyID))
            return false;

        _chunkIDToChunkInfo[chunkID] = {
            .chunk = chunk.get(),
            .editableChunk = std::move(chunk),
            .buffer = std::move(buffer),
            .revision = 1,
            .replaceFileOnSave = true
        };
        if (bodyID != JPH::BodyID::cInvalidBodyID)
            _chunkIDToBodyID[chunkID] = bodyID;
    }

    const u64 fileHash = Util::AssetPath::Hash(GetChunkPath(chunkID));
    if (std::find(_mapHeader.chunkHashes.begin(), _mapHeader.chunkHashes.end(), fileHash) == _mapHeader.chunkHashes.end())
        _mapHeader.chunkHashes.push_back(fileHash);
    _mapHeaderDirty = true;
    _contentGeneration.fetch_add(1, std::memory_order_relaxed);
    return true;
}

bool TerrainLoader::SaveMapHeader()
{
    if (!_mapHeaderDirty)
        return true;

    std::shared_ptr<Bytebuffer> buffer = Bytebuffer::BorrowRuntime(64 * 1024);
    if (!_mapHeader.Save(buffer))
        return false;

    const std::string headerPath = Util::AssetPath::Map(_currentMapInternalName + "/" + _currentMapInternalName + Map::HEADER_FILE_EXTENSION);
    Util::AssetWriter* assetWriter = ServiceLocator::GetAssetWriter();
    if (!assetWriter || !assetWriter->WriteBytes(headerPath, *buffer, Util::AssetWriteTarget::PactOverlay))
        return false;

    _mapHeaderDirty = false;
    _contentGeneration.fetch_add(1, std::memory_order_relaxed);
    return true;
}

bool TerrainLoader::HasMapHeader(std::string_view mapInternalName) const
{
    if (mapInternalName.empty())
        return false;

    PACT::PactStorage* pactStorage = ServiceLocator::GetPactStorage();
    if (!pactStorage)
        return false;

    const std::string headerPath = Util::AssetPath::Map(std::string(mapInternalName) + "/" + std::string(mapInternalName) + Map::HEADER_FILE_EXTENSION);
    PACT::PactFileHandle fileHandle;
    return pactStorage->ReadFile(headerPath, fileHandle) == PACT::PactReadResult::Success;
}

bool TerrainLoader::CreateEmptyMapHeader(std::string_view mapInternalName) const
{
    if (mapInternalName.empty())
        return false;
    if (HasMapHeader(mapInternalName))
        return true;

    Map::MapHeader mapHeader;
    std::shared_ptr<Bytebuffer> buffer = Bytebuffer::BorrowRuntime(64 * 1024);
    if (!mapHeader.Save(buffer))
        return false;

    const std::string headerPath = Util::AssetPath::Map(std::string(mapInternalName) + "/" + std::string(mapInternalName) + Map::HEADER_FILE_EXTENSION);
    Util::AssetWriter* assetWriter = ServiceLocator::GetAssetWriter();
    return assetWriter && assetWriter->WriteBytes(headerPath, *buffer, Util::AssetWriteTarget::PactOverlay);
}

void TerrainLoader::LoadPartialMapRequest(const LoadRequestInternal& request)
{
    assert(request.loadType == LoadType::Partial);
    assert(request.mapName.size() > 0);
    assert(request.chunkGridStartPos.x <= request.chunkGridEndPos.x);
    assert(request.chunkGridStartPos.y <= request.chunkGridEndPos.y);

    u32 gridWidth = (request.chunkGridEndPos.x - request.chunkGridStartPos.x) + 1;
    u32 gridHeight = (request.chunkGridEndPos.y - request.chunkGridStartPos.y) + 1;
    u32 chunksToLoad = gridHeight * gridWidth;

    struct PartialChunk
    {
        u32 x;
        u32 y;
        u64 hash;
        PACT::PactFileHandle file;
    };

    auto* pactStorage = ServiceLocator::GetPactStorage();
    std::vector<PartialChunk> chunks;
    chunks.reserve(chunksToLoad);
    for (u32 i = 0; i < chunksToLoad; i++)
    {
        u32 chunkX = request.chunkGridStartPos.x + (i % gridWidth);
        u32 chunkY = request.chunkGridStartPos.y + (i / gridHeight);
        std::string chunkPath = Util::AssetPath::Map(request.mapName + "/" + request.mapName + "_" + std::to_string(chunkX) + "_" + std::to_string(chunkY) + ".chunk");
        PartialChunk& chunk = chunks.emplace_back(chunkX, chunkY, Util::AssetPath::Hash(chunkPath));
        if (pactStorage->ReadFile(chunk.hash, chunk.file) != PACT::PactReadResult::Success)
            chunks.pop_back();
    }

    if (chunks.empty())
        return;

    TerrainReserveOffsets reserveOffsets;
    _terrainRenderer->AllocateChunks(static_cast<u32>(chunks.size()), reserveOffsets);
    for (u32 i = 0; i < chunks.size(); i++)
    {
        const PartialChunk& partialChunk = chunks[i];
        const auto* chunk = reinterpret_cast<const Map::Chunk*>(partialChunk.file.GetData());
        _terrainRenderer->AddChunk(static_cast<u32>(partialChunk.hash), chunk, ivec2(partialChunk.x, partialChunk.y), reserveOffsets.chunkDataStartOffset + i, reserveOffsets.cellDataStartOffset + (i * Terrain::CHUNK_NUM_CELLS), reserveOffsets.vertexDataStartOffset + (i * Terrain::CHUNK_NUM_CELLS * Terrain::CELL_NUM_VERTICES));

        for (u32 placementIndex = 0; placementIndex < chunk->placementHeader.numPlacements; placementIndex++)
        {
            u64 placementDataOffset = chunk->placementHeader.dataOffset + (placementIndex * sizeof(Terrain::Placement));
            const auto* placement = reinterpret_cast<const Terrain::Placement*>(static_cast<const u8*>(partialChunk.file.GetData()) + placementDataOffset);
            _modelLoader->LoadPlacement(*placement);
        }
    }
}

bool TerrainLoader::LoadFullMapRequest(const LoadRequestInternal& request)
{
    ZoneScoped;

    assert(request.loadType == LoadType::Full);
    assert(request.mapName.size() > 0);

    std::string mapName = request.mapName;
    StringUtils::ToLower(mapName);
    if (mapName == _currentMapInternalName)
    {
        // The requested map is already current, so do not load it again.
        // If it has finished loading, notify this caller immediately. Otherwise,
        // the load already in progress will send MapLoadedEvent when it finishes.
        if (!_modelLoader->IsTerrainLoading())
        {
            const u32 mapID = ServiceLocator::GetGameRenderer()->GetMapLoader()->GetCurrentMapID();
            ECS::Util::EventUtil::PushEvent(ECS::Components::MapLoadedEvent{ mapID });
        }

        return false;
    }

    auto* pactStorage = ServiceLocator::GetPactStorage();

    std::string mapHeaderPath = Util::AssetPath::Map(mapName + "/" + mapName + Map::HEADER_FILE_EXTENSION);

    PACT::PactFileHandle fileHandle;
    if (pactStorage->ReadFile(mapHeaderPath, fileHandle) != PACT::PactReadResult::Success)
    {
        MapLoader* mapLoader = ServiceLocator::GetGameRenderer()->GetMapLoader();
        mapLoader->ReportLoadFailure(mapLoader->GetCurrentMapID(), ECS::Components::MapLoadFailureReason::MissingHeader);
        return false;
    }

    Map::MapHeader mapHeader;
    std::shared_ptr<Bytebuffer> mapHeaderBuffer = std::make_shared<Bytebuffer>(const_cast<void*>(fileHandle.GetData()), fileHandle.GetSize());
    mapHeaderBuffer->writtenData = fileHandle.GetSize();

    if (!Map::MapHeader::Read(mapHeaderBuffer, mapHeader))
    {
        MapLoader* mapLoader = ServiceLocator::GetGameRenderer()->GetMapLoader();
        mapLoader->ReportLoadFailure(mapLoader->GetCurrentMapID(), ECS::Components::MapLoadFailureReason::InvalidHeader);
        return false;
    }

    u32 numChunks = static_cast<u32>(mapHeader.chunkHashes.size());

    NC_LOG_INFO("TerrainLoader : Started Preparing Chunk Loading");

    std::vector<WorkRequest> workRequests;
    workRequests.reserve(numChunks);

    u32 numChunksToLoad = 0;
    for (u32 i = 0; i < numChunks; i++)
    {
        u64 hash = mapHeader.chunkHashes[i];

        if (!pactStorage->FileExists(hash))
            continue;

        const std::string* chunkPath = pactStorage->GetFilePath(hash);
        if (!chunkPath)
            continue;

        std::string path = *chunkPath;
        std::vector<std::string> splitStrings = StringUtils::SplitString(path, '_');
        u32 numSplitStrings = static_cast<u32>(splitStrings.size());

        u16 chunkX = std::stoi(splitStrings[numSplitStrings - 2]);
        u16 chunkY = std::stoi(splitStrings[numSplitStrings - 1].substr(0, 2));
        u32 chunkID = chunkX + (chunkY * Terrain::CHUNK_NUM_PER_MAP_STRIDE);
        u32 chunkHash = StringUtils::fnv1a_32(path.c_str(), path.size());

        WorkRequest& workRequest = workRequests.emplace_back();
        workRequest.chunkID = chunkID;
        workRequest.chunkHash = chunkHash;
        workRequest.fileHash = hash;

        numChunksToLoad++;
    }

    NC_LOG_INFO("TerrainLoader : Finished Preparing Chunk Loading");

    if (numChunksToLoad == 0 && numChunks != 0)
    {
        NC_LOG_ERROR("TerrainLoader : Map '{0}' could not prepare chunks", request.mapName);
        MapLoader* mapLoader = ServiceLocator::GetGameRenderer()->GetMapLoader();
        mapLoader->ReportLoadFailure(mapLoader->GetCurrentMapID(), ECS::Components::MapLoadFailureReason::NoAvailableChunks);
        return false;
    }

    auto* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;

    Clear();

    _currentMapInternalName = mapName;
    _mapHeader = std::move(mapHeader);
    _mapHeaderDirty = false;
    registry->ctx().get<ECS::Singletons::JoltState>().ResetPhysicsTelemetry(mapName);
    NotifyCurrentMapChanged();
    _numChunksToLoad = numChunksToLoad;

    Scripting::Zenith* zenith = Scripting::Util::Zenith::GetGlobal();
    zenith->CallEvent(MetaGen::Game::Lua::GameEvent::MapLoading, MetaGen::Game::Lua::GameEventDataMapLoading{ .mapInternalName = mapName });

    NC_LOG_INFO("TerrainLoader : Started Chunk Queueing");

    auto& activeCamera = registry->ctx().get<ECS::Singletons::ActiveCamera>();
    vec3 cameraPos = registry->get<ECS::Components::Transform>(activeCamera.entity).GetWorldPosition();

    vec2 playerChunkGlobalPos = Util::Map::WorldPositionToChunkGlobalPos(cameraPos);
    vec2 playerChunkPos = Util::Map::GetChunkIndicesFromAdtPosition(playerChunkGlobalPos);

    std::sort(workRequests.begin(), workRequests.end(), [&playerChunkPos](const WorkRequest& a, const WorkRequest& b)
    {
        u32 aChunkX = a.chunkID % Terrain::CHUNK_NUM_PER_MAP_STRIDE;
        u32 aChunkY = a.chunkID / Terrain::CHUNK_NUM_PER_MAP_STRIDE;

        u32 bChunkX = b.chunkID % Terrain::CHUNK_NUM_PER_MAP_STRIDE;
        u32 bChunkY = b.chunkID / Terrain::CHUNK_NUM_PER_MAP_STRIDE;

        vec2 aChunkPos = vec2(aChunkX, aChunkY);
        vec2 bChunkPos = vec2(bChunkX, bChunkY);
        
        u32 distA = static_cast<u32>(glm::abs(glm::distance(aChunkPos, playerChunkPos)));
        u32 distB = static_cast<u32>(glm::abs(glm::distance(bChunkPos, playerChunkPos)));

        return distA < distB;
    });

    for (WorkRequest& workRequest : workRequests)
    {
        _requestedChunkHashes.insert(workRequest.chunkHash);
        _pendingWorkRequests.enqueue(std::move(workRequest));
    }
    
    NC_LOG_INFO("TerrainLoader : Finished Chunk Queueing");

    _terrainRenderer->Reserve(numChunksToLoad);
    return true;
}
