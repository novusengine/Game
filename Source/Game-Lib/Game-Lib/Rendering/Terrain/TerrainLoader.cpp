#include "TerrainLoader.h"
#include "TerrainRenderer.h"

#include "Game-Lib/Application/EnttRegistries.h"
#include "Game-Lib/ECS/Components/AnimationData.h"
#include "Game-Lib/ECS/Components/Events.h"
#include "Game-Lib/ECS/Components//Model.h"
#include "Game-Lib/ECS/Singletons/ActiveCamera.h"
#include "Game-Lib/ECS/Singletons/AnimationSingleton.h"
#include "Game-Lib/ECS/Singletons/JoltState.h"
#include "Game-Lib/ECS/Systems/CharacterController.h"
#include "Game-Lib/ECS/Util/EventUtil.h"
#include "Game-Lib/ECS/Util/Transforms.h"
#include "Game-Lib/Editor/EditorHandler.h"
#include "Game-Lib/Editor/Inspector.h"
#include "Game-Lib/Rendering/Debug/DebugRenderer.h"
#include "Game-Lib/Rendering/Debug/JoltDebugRenderer.h"
#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Rendering/Model/ModelLoader.h"
#include "Game-Lib/Rendering/Liquid/LiquidLoader.h"
#include "Game-Lib/Gameplay/MapLoader.h"
#include "Game-Lib/Util/JoltStream.h"
#include "Game-Lib/Util/MapUtil.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Base/CVarSystem/CVarSystem.h>
#include <Base/Memory/FileReader.h>
#include <Base/Util/StringUtils.h>

#include <FileFormat/Novus/Map/Map.h>
#include <FileFormat/Novus/Map/MapChunk.h>

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
    , _loadedChunkRequests()
    , _pendingWorkRequests()
{
    _chunkIDToLoadedID.reserve(4096);
    _chunkIDToBodyID.reserve(4096);
    _chunkIDToChunkInfo.reserve(4096);
}

void TerrainLoader::Clear()
{
    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
    auto& joltState = registry->ctx().get<ECS::Singletons::JoltState>();
    
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
    _requestedChunkHashes.clear();
    
    IOLoadRequest loadResult;
    while (_loadedChunkRequests.try_dequeue(loadResult)) { }
    
    WorkRequest workRequest;
    while (_pendingWorkRequests.try_dequeue(workRequest)) { }
    
    _chunkIDToLoadedID.clear();
    _chunkIDToBodyID.clear();
    
    for (auto& pair : _chunkIDToChunkInfo)
    {
        auto& chunkInfo = pair.second;

        chunkInfo.chunk = nullptr;

        if (chunkInfo.data)
            chunkInfo.data.reset();
    }
    
    _chunkIDToChunkInfo.clear();
    
    ServiceLocator::GetGameRenderer()->GetModelLoader()->Clear();
    ServiceLocator::GetGameRenderer()->GetLiquidLoader()->Clear();
    ServiceLocator::GetGameRenderer()->GetJoltDebugRenderer()->Clear();
    _terrainRenderer->Clear();
    
    Editor::EditorHandler* editorHandler = ServiceLocator::GetEditorHandler();
    editorHandler->GetInspector()->ClearSelection();
    
    _currentMapInternalName.clear();
    
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
            LoadFullMapRequest(loadRequest);
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

        enki::TaskSet loadChunksTask(maxChunkLoadsThisTick, [this, &bodyInterface, &reserveOffsets, physicsEnabled](enki::TaskSetPartition range, uint32_t threadNum)
        {
            ZoneScopedN("Load Chunk Task");
            u32 numSuccessfulLoads = 0;

            WorkRequest workRequest;
            for (u32 i = range.start; i < range.end; i++)
            {
                if (!_pendingWorkRequests.try_dequeue(workRequest))
                    break;

                ZoneScopedN("Load Chunk Worker");

                if (!_requestedChunkHashes.contains(workRequest.chunkHash))
                    continue;

                u32 chunkX = workRequest.chunkID % Terrain::CHUNK_NUM_PER_MAP_STRIDE;
                u32 chunkY = workRequest.chunkID / Terrain::CHUNK_NUM_PER_MAP_STRIDE;

                Map::Chunk* chunk = reinterpret_cast<Map::Chunk*>(workRequest.data->GetDataPointer());
                if (chunk->header.type != FileHeader::Type::MapChunk)
                {
                    NC_LOG_ERROR("TerrainLoader : Invalid Chunk Header");
                    continue;
                }

                if (chunk->header.version != Map::Chunk::CURRENT_VERSION)
                {
                    NC_LOG_ERROR("TerrainLoader : Invalid Chunk Version. Expected {0} but got {1}", Map::Chunk::CURRENT_VERSION, chunk->header.version);
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
                        Bytebuffer physicsBuffer = Bytebuffer(chunk->physicsHeader.GetPhysicsData(workRequest.data), numPhysicsBytes);
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
                        _chunkIDToChunkInfo[workRequest.chunkID] = { .chunk = chunk, .data = workRequest.data };
                        _chunkIDToLoadedID[workRequest.chunkID] = chunkDataID;

                        if (physicsBodyID != JPH::BodyID::cInvalidBodyID)
                            _chunkIDToBodyID[workRequest.chunkID] = physicsBodyID;
                    }

                    {
                        ZoneScopedN("Load Chunk Placements");

                        u32 numPlacements = chunk->placementHeader.numPlacements;
                        for (u32 placementIndex = 0; placementIndex < numPlacements; placementIndex++)
                        {
                            auto* placement = chunk->placementHeader.GetPlacement(workRequest.data, placementIndex);
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
                            _liquidLoader->LoadFromChunk(chunkX, chunkY, workRequest.data, chunk->liquidHeader);
                        }
                    }
                }

                numSuccessfulLoads++;
            }

            _numChunksLoaded += numSuccessfulLoads;
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

            u32 mapID = ServiceLocator::GetGameRenderer()->GetMapLoader()->GetCurrentMapID();
            ECS::Util::EventUtil::PushEvent(ECS::Components::MapLoadedEvent{ mapID });

            NC_LOG_INFO("TerrainLoader : Loaded {0}/{1} chunks", numChunksLoadedAfter, numChunksLoadedAfter);
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

void TerrainLoader::LoadPartialMapRequest(const LoadRequestInternal& request)
{
    enki::TaskScheduler* taskScheduler = ServiceLocator::GetTaskScheduler();

    std::string mapName = request.mapName;
    fs::path absoluteMapPath = fs::absolute("Data/Map/" + mapName);

    assert(request.loadType == LoadType::Partial);
    assert(request.mapName.size() > 0);
    assert(request.chunkGridStartPos.x <= request.chunkGridEndPos.x);
    assert(request.chunkGridStartPos.y <= request.chunkGridEndPos.y);

    u32 gridWidth = (request.chunkGridEndPos.x - request.chunkGridStartPos.x) + 1;
    u32 gridHeight = (request.chunkGridEndPos.y - request.chunkGridStartPos.y) + 1;

    u32 startChunkNum = request.chunkGridStartPos.x + (request.chunkGridStartPos.y * Terrain::CHUNK_NUM_PER_MAP_STRIDE);
    u32 chunksToLoad = gridHeight * gridWidth;

    std::atomic<u32> numExistingChunks = 0;
    enki::TaskSet countValidChunksTask(chunksToLoad, [&, startChunkNum](enki::TaskSetPartition range, uint32_t threadNum)
    {
        u32 localNumExistingChunks = 0;

        std::string chunkLocalPathStr = "";
        chunkLocalPathStr.reserve(128);

        for (u32 i = range.start; i < range.end; i++)
        {
            u32 chunkX = request.chunkGridStartPos.x + (i % gridWidth);
            u32 chunkY = request.chunkGridStartPos.y + (i / gridHeight);

            chunkLocalPathStr.clear();
            chunkLocalPathStr += mapName + "_" + std::to_string(chunkX) + "_" + std::to_string(chunkY) + ".chunk";

            fs::path chunkLocalPath = chunkLocalPathStr;
            fs::path chunkPath = absoluteMapPath / chunkLocalPath;

            if (!fs::exists(chunkPath))
                continue;

            localNumExistingChunks++;
        }

        numExistingChunks.fetch_add(localNumExistingChunks);
    });

    NC_LOG_INFO("TerrainLoader : Started Preparing Chunk Loading");
    taskScheduler->AddTaskSetToPipe(&countValidChunksTask);
    taskScheduler->WaitforTask(&countValidChunksTask);
    NC_LOG_INFO("TerrainLoader : Finished Preparing Chunk Loading");

    u32 numChunksToLoad = numExistingChunks.load();
    if (numChunksToLoad == 0)
    {
        NC_LOG_ERROR("TerrainLoader : Failed to prepare chunks for map '{0}'", request.mapName);
        return;
    }

    TerrainReserveOffsets reserveOffsets;
    _terrainRenderer->AllocateChunks(numChunksToLoad, reserveOffsets);

    enki::TaskSet loadChunksTask(chunksToLoad, [&, startChunkNum](enki::TaskSetPartition range, uint32_t threadNum)
    {
        std::string chunkLocalPathStr = "";
        chunkLocalPathStr.reserve(128);

        for (u32 i = range.start; i < range.end; i++)
        {
            u32 chunkX = request.chunkGridStartPos.x + (i % gridWidth);
            u32 chunkY = request.chunkGridStartPos.y + (i / gridHeight);

            chunkLocalPathStr.clear();
            chunkLocalPathStr += mapName + "_" + std::to_string(chunkX) + "_" + std::to_string(chunkY) + ".chunk";

            fs::path chunkLocalPath = chunkLocalPathStr;
            fs::path chunkPath = absoluteMapPath / chunkLocalPath;

            if (!fs::exists(chunkPath))
                continue;

            std::string chunkPathStr = chunkPath.string();

            FileReader reader(chunkPathStr);
            if (reader.Open())
            {
                size_t bufferSize = reader.Length();

                std::shared_ptr<Bytebuffer> chunkBuffer = Bytebuffer::BorrowRuntime(bufferSize);
                reader.Read(chunkBuffer.get(), bufferSize);

                u32 chunkHash = StringUtils::fnv1a_32(chunkPathStr.c_str(), chunkPathStr.size());
                Map::Chunk* chunk = reinterpret_cast<Map::Chunk*>(chunkBuffer->GetDataPointer());

                u32 chunkDataIndex = reserveOffsets.chunkDataStartOffset + i;
                u32 cellDataStartIndex = reserveOffsets.cellDataStartOffset + (i * Terrain::CHUNK_NUM_CELLS);
                u32 vertexDataStartIndex = reserveOffsets.vertexDataStartOffset + (i * Terrain::CHUNK_NUM_CELLS * Terrain::CELL_NUM_VERTICES);

                u32 chunkDataID = _terrainRenderer->AddChunk(chunkHash, chunk, ivec2(chunkX, chunkY), chunkDataIndex, cellDataStartIndex, vertexDataStartIndex);

                u32 numPlacements = chunk->placementHeader.numPlacements;
                for (u32 placementIndex = 0; placementIndex < numPlacements; placementIndex++)
                {
                    u64 placementDataOffset = chunk->placementHeader.dataOffset + (placementIndex * sizeof(Terrain::Placement));
                    auto* placement = reinterpret_cast<Terrain::Placement*>(&chunkBuffer->GetDataPointer()[placementDataOffset]);
                    _modelLoader->LoadPlacement(*placement);
                }
            }
        }
    });

    NC_LOG_INFO("TerrainLoader : Started Chunk Loading");
    taskScheduler->AddTaskSetToPipe(&loadChunksTask);
    taskScheduler->WaitforTask(&loadChunksTask);
    NC_LOG_INFO("TerrainLoader : Finished Chunk Loading");
}

bool TerrainLoader::LoadFullMapRequest(const LoadRequestInternal& request)
{
    ZoneScoped;
    enki::TaskScheduler* taskScheduler = ServiceLocator::GetTaskScheduler();

    assert(request.loadType == LoadType::Full);
    assert(request.mapName.size() > 0);

    const std::string& mapName = request.mapName;
    if (mapName == _currentMapInternalName)
    {
        return false;
    }

    fs::path absoluteMapPath = fs::absolute("Data/Map/" + mapName);
    if (!fs::is_directory(absoluteMapPath))
    {
        NC_LOG_ERROR("TerrainLoader : Failed to find '{0}' folder", absoluteMapPath.string());
        return false;
    }

    std::vector<fs::path> paths;
    fs::directory_iterator dirpos{ absoluteMapPath };
    std::copy(begin(dirpos), end(dirpos), std::back_inserter(paths));

    u32 numPaths = static_cast<u32>(paths.size());
    std::atomic<u32> numExistingChunks = 0;

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;

    i32 physicsEnabled = *CVarSystem::Get()->GetIntCVar(CVarCategory::Client | CVarCategory::Physics, "enabled"_h);
    auto& joltState = registry->ctx().get<ECS::Singletons::JoltState>();
    JPH::BodyInterface& bodyInterface = joltState.physicsSystem.GetBodyInterface();

    enki::TaskSet countValidChunksTask(numPaths, [&](enki::TaskSetPartition range, uint32_t threadNum)
    {
        for (u32 i = range.start; i < range.end; i++)
        {
            const fs::path& path = paths[i];

            if (path.extension() != ".chunk")
                continue;

            numExistingChunks++;
        }
    });

    enki::TaskSet loadChunksTask(numPaths, [&, physicsEnabled](enki::TaskSetPartition range, uint32_t threadNum)
    {
        for (u32 i = range.start; i < range.end; i++)
        {
            const fs::path& path = paths[i];

            if (path.extension() != ".chunk")
                continue;

            std::string pathStr = path.string();

            std::vector<std::string> splitStrings = StringUtils::SplitString(pathStr, '_');
            u32 numSplitStrings = static_cast<u32>(splitStrings.size());

            u16 chunkX = std::stoi(splitStrings[numSplitStrings - 2]);
            u16 chunkY = std::stoi(splitStrings[numSplitStrings - 1].substr(0, 2));
            u32 chunkHash = StringUtils::fnv1a_32(pathStr.c_str(), pathStr.size());

            IOLoadRequest loadRequest;
            loadRequest.userdata = chunkHash | (static_cast<u64>(chunkX) << 32) | (static_cast<u64>(chunkY) << 48);
            loadRequest.path = std::move(pathStr);

            _loadedChunkRequests.enqueue(loadRequest);
        }

        return true;
    });

    NC_LOG_INFO("TerrainLoader : Started Preparing Chunk Loading");
    taskScheduler->AddTaskSetToPipe(&countValidChunksTask);
    taskScheduler->WaitforTask(&countValidChunksTask);
    NC_LOG_INFO("TerrainLoader : Finished Preparing Chunk Loading");

    u32 numChunksToLoad = numExistingChunks.load();
    if (numChunksToLoad == 0)
    {
        NC_LOG_ERROR("TerrainLoader : Failed to prepare chunks for map '{0}'", request.mapName);
        return false;
    }

    Clear();

    const auto& storage = registry->storage<entt::entity>(); // Access internal entity storage
    NC_LOG_INFO("Allocated Entities {0}", storage.free_list());

    _currentMapInternalName = mapName;
    _numChunksToLoad = numChunksToLoad;

    NC_LOG_INFO("TerrainLoader : Started Chunk Queueing");

    taskScheduler->AddTaskSetToPipe(&loadChunksTask);
    taskScheduler->WaitforTask(&loadChunksTask);

    std::vector<IOLoadRequest> loadRequests(numExistingChunks);
    bool dequeueResult = _loadedChunkRequests.try_dequeue_bulk(loadRequests.data(), numExistingChunks);
    NC_ASSERT(dequeueResult, "Failed to dequeue all loaded chunks");

    auto& activeCamera = registry->ctx().get<ECS::Singletons::ActiveCamera>();
    vec3 cameraPos = registry->get<ECS::Components::Transform>(activeCamera.entity).GetWorldPosition();

    vec2 playerChunkGlobalPos = Util::Map::WorldPositionToChunkGlobalPos(cameraPos);
    vec2 playerChunkPos = Util::Map::GetChunkIndicesFromAdtPosition(playerChunkGlobalPos);

    std::sort(loadRequests.begin(), loadRequests.end(), [&playerChunkPos](const IOLoadRequest& a, const IOLoadRequest& b)
    {
        u32 aChunkX = (a.userdata >> 32) & 0xFFFF;
        u32 aChunkY = (a.userdata >> 48) & 0xFFFF;

        u32 bChunkX = (b.userdata >> 32) & 0xFFFF;
        u32 bChunkY = (b.userdata >> 48) & 0xFFFF;

        vec2 aChunkPos = vec2(aChunkX, aChunkY);
        vec2 bChunkPos = vec2(bChunkX, bChunkY);
        
        u32 distA = static_cast<u32>(glm::abs(glm::distance(aChunkPos, playerChunkPos)));
        u32 distB = static_cast<u32>(glm::abs(glm::distance(bChunkPos, playerChunkPos)));

        return distA < distB;
    });

    IOLoader* ioLoader = ServiceLocator::GetIOLoader();
    for (IOLoadRequest& loadRequest : loadRequests)
    {
        u32 chunkHash = loadRequest.userdata & 0xFFFFFFFF;

        u32 chunkX = (loadRequest.userdata >> 32) & 0xFFFF;
        u32 chunkY = (loadRequest.userdata >> 48) & 0xFFFF;
        u32 chunkID = chunkX + (chunkY * Terrain::CHUNK_NUM_PER_MAP_STRIDE);

        loadRequest.userdata &= 0xFFFFFFFF;
        loadRequest.userdata |= static_cast<u64>(chunkID) << 32;
        loadRequest.callback = std::bind(&TerrainLoader::IOLoadCallback, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4);

        _requestedChunkHashes.insert(chunkHash);

        ioLoader->RequestLoad(loadRequest);
    }
    
    NC_LOG_INFO("TerrainLoader : Finished Chunk Queueing");

    _terrainRenderer->Reserve(numChunksToLoad);
    return true;
}

void TerrainLoader::IOLoadCallback(bool result, std::shared_ptr<Bytebuffer> buffer, const std::string& path, u64 userdata)
{
    ZoneScoped;
    u32 chunkID = (userdata >> 32) & 0xFFFFFFFF;
    u32 chunkHash = userdata & 0xFFFFFFFF;

    if (!_requestedChunkHashes.contains(chunkHash))
        return;

    if (result)
    {
        WorkRequest workRequest =
        {
            .chunkID = chunkID,
            .chunkHash = chunkHash,
            .data = std::move(buffer)
        };

        _pendingWorkRequests.enqueue(workRequest);

        //NC_LOG_INFO("TerrainLoader : Loaded ({0}, {1})", chunkID, path);
    }
    else
    {
        NC_LOG_WARNING("TerrainLoader : Failed to Load ({0}, {1})", chunkID, path)
    }
}
