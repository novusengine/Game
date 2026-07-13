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

#include <atomic>
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

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

    _chunkIDToChunkInfo.clear();
    
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
    if (numPendingRequests > 0)
    {
        ZoneScopedN("PendingWork");
        enki::TaskScheduler* taskScheduler = ServiceLocator::GetTaskScheduler();
        i32 physicsEnabled = *CVarSystem::Get()->GetIntCVar(CVarCategory::Client | CVarCategory::Physics, "enabled"_h);

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& joltState = registry->ctx().get<ECS::Singletons::JoltState>();
        JPH::BodyInterface& bodyInterface = joltState.physicsSystem.GetBodyInterface();

        u32 maxChunkLoadsThisTick = glm::min(numPendingRequests, 64u);

        TerrainReserveOffsets reserveOffsets;
        _terrainRenderer->AllocateChunks(maxChunkLoadsThisTick, reserveOffsets);
        _chunkIDToLoadedID.reserve(_chunkIDToLoadedID.size() + maxChunkLoadsThisTick);
        _chunkIDToBodyID.reserve(_chunkIDToBodyID.size() + maxChunkLoadsThisTick);

        enki::TaskSet loadChunksTask(maxChunkLoadsThisTick, [this, &bodyInterface, &joltState, &reserveOffsets, physicsEnabled](enki::TaskSetPartition range, uint32_t threadNum)
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
                {
                    std::scoped_lock lock(_pactReadMutex);
                    if (ServiceLocator::GetPactStorage()->ReadFile(workRequest.fileHash, *workRequest.fileHandle) != PACT::PactReadResult::Success)
                    {
                        NC_LOG_ERROR("TerrainLoader : Failed to read chunk asset {0}", workRequest.fileHash);
                        numFailedLoads++;
                        continue;
                    }
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

                u32 numPhysicsBytes = chunk->physicsHeader.numBytes;
                if (physicsEnabled && numPhysicsBytes > 0)
                {
                    ZoneScopedN("Load Physics");
                    JPH::ShapeRefC shape = nullptr;
                    JPH::Body* body = nullptr;

                    {
                        ZoneScopedN("Read Shape");
                        Bytebuffer physicsBuffer = Bytebuffer(chunk->physicsHeader.GetPhysicsData(workRequest.buffer), numPhysicsBytes);
                        physicsBuffer.SkipWrite(numPhysicsBytes);

                        JoltStreamIn streamIn(&physicsBuffer);

                        {
                            ZoneScopedN("Restore Shape");
                            JPH::Shape::IDToShapeMap shapeMap;
                            JPH::Shape::IDToMaterialMap materialMap;
                            JPH::ShapeSettings::ShapeResult shapeResult = JPH::Shape::sRestoreWithChildren(streamIn, shapeMap, materialMap);
                            shape = shapeResult.Get(); // We don't expect an error here, but you can check floor_shape_result for HasError() / GetError()
                        }
                    }
                   
                    {
                        ZoneScopedN("Create Body");
                        // Create the settings for the body itself. Note that here you can also set other properties like the restitution / friction.
                        vec2 chunkPos = Util::Map::GetChunkPosition(workRequest.chunkID);
                        JPH::BodyCreationSettings bodySettings(shape, JPH::RVec3(chunkPos.x, 0.0f, chunkPos.y), JPH::Quat::sIdentity(), JPH::EMotionType::Static, Jolt::Layers::NON_MOVING);

                        // Create the actual rigid body
                        body = bodyInterface.CreateBody(bodySettings); // Note that if we run out of bodies this can return nullptr
                        joltState.RecordBodyCreate(ECS::Singletons::JoltBodyTelemetrySource::TerrainChunk, body != nullptr);
                    }

                    if (body)
                    {
                        ZoneScopedN("Add Body");
                        body->SetUserData(std::numeric_limits<JPH::uint64>().max());
                        body->SetFriction(0.8f);

                        JPH::BodyID bodyID = body->GetID();

                        bodyInterface.AddBody(bodyID, JPH::EActivation::Activate);
                        physicsBodyID = bodyID.GetIndexAndSequenceNumber();
                    }
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
        _terrainRenderer->AddChunk(static_cast<u32>(partialChunk.hash), chunk, ivec2(partialChunk.x, partialChunk.y),
            reserveOffsets.chunkDataStartOffset + i,
            reserveOffsets.cellDataStartOffset + (i * Terrain::CHUNK_NUM_CELLS),
            reserveOffsets.vertexDataStartOffset + (i * Terrain::CHUNK_NUM_CELLS * Terrain::CELL_NUM_VERTICES));

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

    const std::string& mapName = request.mapName;
    if (mapName == _currentMapInternalName)
    {
        return false;
    }

    auto* pactStorage = ServiceLocator::GetPactStorage();

    std::string mapHeaderPath = Util::AssetPath::Map(mapName + "/" + mapName + Map::HEADER_FILE_EXTENSION);

    PACT::PactFileHandle fileHandle;
    if (pactStorage->ReadFile(mapHeaderPath, fileHandle) != PACT::PactReadResult::Success)
        return false;

    Map::MapHeader mapHeader;
    std::shared_ptr<Bytebuffer> mapHeaderBuffer = std::make_shared<Bytebuffer>(const_cast<void*>(fileHandle.GetData()), fileHandle.GetSize());
    mapHeaderBuffer->writtenData = fileHandle.GetSize();

    if (!Map::MapHeader::Read(mapHeaderBuffer, mapHeader))
        return false;

    u32 numChunks = static_cast<u32>(mapHeader.chunkHashes.size());
    if (numChunks == 0)
    {
        NC_LOG_ERROR("TerrainLoader : Map '{0}' has no chunks", request.mapName);
        return false;
    }

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

    if (numChunksToLoad == 0)
    {
        NC_LOG_ERROR("TerrainLoader : Map '{0}' could not prepare chunks", request.mapName);
        return false;
    }

    auto* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;

    Clear();

    _currentMapInternalName = mapName;
    registry->ctx().get<ECS::Singletons::JoltState>().ResetPhysicsTelemetry(mapName);
    NotifyCurrentMapChanged();
    _numChunksToLoad = numChunksToLoad;

    Scripting::Zenith* zenith = Scripting::Util::Zenith::GetGlobal();
    zenith->CallEvent(MetaGen::Game::Lua::GameEvent::MapLoading, MetaGen::Game::Lua::GameEventDataMapLoading{
        .mapInternalName = mapName
    });

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
