#include "TerrainLoader.h"
#include "TerrainRenderer.h"

#include "Game-Lib/Application/EnttRegistries.h"
#include "Game-Lib/ECS/Components/Events.h"
#include "Game-Lib/ECS/Singletons/JoltState.h"
#include "Game-Lib/ECS/Systems/CharacterController.h"
#include "Game-Lib/ECS/Util/EventUtil.h"
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
{
    _chunkIDToLoadedID.reserve(4096);
    _chunkIDToBodyID.reserve(4096);
    _chunkIDToChunkPtr.reserve(4096);
}

void TerrainLoader::Clear()
{
    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
    auto& joltState = registry->ctx().get<ECS::Singletons::JoltState>();
    JPH::BodyInterface& bodyInterface = joltState.physicsSystem.GetBodyInterface();

    LoadRequestInternal loadRequest;
    while (_requests.try_dequeue(loadRequest)) { }

    for (auto& pair : _chunkIDToBodyID)
    {
        JPH::BodyID id = static_cast<JPH::BodyID>(pair.second);

        bodyInterface.RemoveBody(id);
        bodyInterface.DestroyBody(id);
    }

    _chunkIDToLoadedID.clear();
    _chunkIDToBodyID.clear();

    for (auto& pair : _chunkIDToChunkPtr)
    {
        if (pair.second)
            delete pair.second;
    }

    _chunkIDToChunkPtr.clear();

    ServiceLocator::GetGameRenderer()->GetModelLoader()->Clear();
    ServiceLocator::GetGameRenderer()->GetLiquidLoader()->Clear();
    ServiceLocator::GetGameRenderer()->GetJoltDebugRenderer()->Clear();
    _terrainRenderer->Clear();

    Editor::EditorHandler* editorHandler = ServiceLocator::GetEditorHandler();
    editorHandler->GetInspector()->ClearSelection();

    _currentMapInternalName.clear();
}

void TerrainLoader::Update(f32 deltaTime)
{
    ZoneScoped;

    LoadRequestInternal loadRequest;

    while (_requests.try_dequeue(loadRequest))
    {
        if (loadRequest.loadType == LoadType::Partial)
        {
            // TODO : Disabled as this needs fixing
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
    _terrainRenderer->Reserve(numChunksToLoad, reserveOffsets);

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

                for (Terrain::Placement& placement : chunk->complexModelPlacements)
                {
                    _modelLoader->LoadPlacement(placement);
                }
            }
        }
    });

    NC_LOG_INFO("TerrainLoader : Started Chunk Loading");
    taskScheduler->AddTaskSetToPipe(&loadChunksTask);
    taskScheduler->WaitforTask(&loadChunksTask);
    NC_LOG_INFO("TerrainLoader : Finished Chunk Loading");
}

void TerrainLoader::LoadFullMapRequest(const LoadRequestInternal& request)
{
    enki::TaskScheduler* taskScheduler = ServiceLocator::GetTaskScheduler();

    assert(request.loadType == LoadType::Full);
    assert(request.mapName.size() > 0);

    const std::string& mapName = request.mapName;
    if (mapName == _currentMapInternalName)
    {
        return;
    }

    fs::path absoluteMapPath = fs::absolute("Data/Map/" + mapName);
    if (!fs::is_directory(absoluteMapPath))
    {
        NC_LOG_ERROR("TerrainLoader : Failed to find '{0}' folder", absoluteMapPath.string());
        return;
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
    PrepareForChunks(LoadType::Full, numPaths, reserveOffsets);
    _currentMapInternalName = mapName;

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

            u32 chunkX = std::stoi(splitStrings[numSplitStrings - 2]);
            u32 chunkY = std::stoi(splitStrings[numSplitStrings - 1].substr(0, 2));
            u32 chunkID = chunkX + (chunkY * Terrain::CHUNK_NUM_PER_MAP_STRIDE);

            std::string chunkPathStr = path.string();

            FileReader reader(chunkPathStr);
            if (reader.Open())
            {
                size_t bufferSize = reader.Length();

                std::shared_ptr<Bytebuffer> chunkBuffer = Bytebuffer::BorrowRuntime(bufferSize);
                reader.Read(chunkBuffer.get(), bufferSize);

                u32 chunkHash = StringUtils::fnv1a_32(chunkPathStr.c_str(), chunkPathStr.size());
                Map::Chunk* chunk = new Map::Chunk();
                Map::Chunk::Read(chunkBuffer, *chunk);

                _chunkIDToChunkPtr[chunkID] = chunk;

                // Load into Jolt
                u32 numPhysicsBytes = static_cast<u32>(chunk->physicsData.size());
                if (physicsEnabled && numPhysicsBytes > 0)
                {
                    std::shared_ptr<Bytebuffer> physicsBuffer = std::make_shared<Bytebuffer>(chunk->physicsData.data(), numPhysicsBytes);
                    JoltStreamIn streamIn(physicsBuffer);

                    JPH::Shape::IDToShapeMap shapeMap;
                    JPH::Shape::IDToMaterialMap materialMap;

                    JPH::ShapeSettings::ShapeResult shapeResult = JPH::Shape::sRestoreWithChildren(streamIn, shapeMap, materialMap);
                    JPH::ShapeRefC shape = shapeResult.Get(); // We don't expect an error here, but you can check floor_shape_result for HasError() / GetError()

                    // Create the settings for the body itself. Note that here you can also set other properties like the restitution / friction.
                    vec2 chunkPos = Util::Map::GetChunkPosition(chunkID);
                    JPH::BodyCreationSettings bodySettings(shape, JPH::RVec3(chunkPos.x, 0.0f, chunkPos.y), JPH::Quat::sIdentity(), JPH::EMotionType::Static, Jolt::Layers::NON_MOVING);

                    // Create the actual rigid body
                    JPH::Body* body = bodyInterface.CreateBody(bodySettings); // Note that if we run out of bodies this can return nullptr

                    if (body)
                    {
                        body->SetUserData(std::numeric_limits<JPH::uint64>().max());
                        body->SetFriction(0.8f);

                        JPH::BodyID bodyID = body->GetID();

                        bodyInterface.AddBody(bodyID, JPH::EActivation::Activate);
                        _chunkIDToBodyID[chunkID] = bodyID.GetIndexAndSequenceNumber();
                    }
                }

                // Load into Terrain Renderer
                {
                    u32 chunkDataIndex = reserveOffsets.chunkDataStartOffset + i;
                    u32 cellDataStartIndex = reserveOffsets.cellDataStartOffset + (i * Terrain::CHUNK_NUM_CELLS);
                    u32 vertexDataStartIndex = reserveOffsets.vertexDataStartOffset + (i * Terrain::CHUNK_NUM_CELLS * Terrain::CELL_NUM_VERTICES);

                    u32 chunkDataID = _terrainRenderer->AddChunk(chunkHash, chunk, ivec2(chunkX, chunkY), chunkDataIndex, cellDataStartIndex, vertexDataStartIndex);
                    _chunkIDToLoadedID[chunkID] = chunkDataID;

                    u32 numMapObjectPlacements = chunk->numMapObjectPlacements;
                    u32 numComplexModelPlacements = chunk->numComplexModelPlacements;

                    size_t mapObjectOffset = sizeof(Map::Chunk) - ((sizeof(std::vector<Terrain::Placement>) * 2) + sizeof(Map::LiquidInfo));

                    if (numMapObjectPlacements)
                    {
                        for (u32 j = 0; j < numMapObjectPlacements; j++)
                        {
                            const Terrain::Placement& placement = chunk->mapObjectPlacements[j];
                            _modelLoader->LoadPlacement(placement);
                        }
                    }

                    if (numComplexModelPlacements)
                    {
                        size_t cModelOffset = mapObjectOffset + (numMapObjectPlacements * sizeof(Terrain::Placement));

                        for (u32 j = 0; j < numComplexModelPlacements; j++)
                        {
                            const Terrain::Placement& placement = chunk->complexModelPlacements[j];
                            _modelLoader->LoadPlacement(placement);
                        }
                    }

                    // Load Liquid
                    {
                        u32 numLiquidHeaders = static_cast<u32>(chunk->liquidInfo.headers.size());

                        if (numLiquidHeaders != 0 && numLiquidHeaders != 256)
                        {
                            NC_LOG_CRITICAL("LiquidInfo should always contain either 0 or 256 liquid headers, but it contained {0} liquid headers", numLiquidHeaders);
                        }

                        if (numLiquidHeaders == 256)
                        {
                            _liquidLoader->LoadFromChunk(chunkX, chunkY, &chunk->liquidInfo);
                        }
                    }
                }
            }
        }

        return true;
    });

    NC_LOG_INFO("TerrainLoader : Started Chunk Loading");
    taskScheduler->AddTaskSetToPipe(&loadChunksTask);
    taskScheduler->WaitforTask(&loadChunksTask);

    u32 mapID = ServiceLocator::GetGameRenderer()->GetMapLoader()->GetCurrentMapID();
    ECS::Util::EventUtil::PushEvent(ECS::Components::MapLoadedEvent{ mapID });

    i32 physicsOptimizeBP = *CVarSystem::Get()->GetIntCVar(CVarCategory::Client | CVarCategory::Physics, "optimizeBP"_h);
    if (physicsEnabled && physicsOptimizeBP)
    {
        joltState.physicsSystem.OptimizeBroadPhase();
    }

    NC_LOG_INFO("TerrainLoader : Finished Chunk Loading");
}

void TerrainLoader::PrepareForChunks(LoadType loadType, u32 numChunks, TerrainReserveOffsets& reserveOffsets)
{
    if (loadType == LoadType::Partial)
    {

    }
    else if (loadType == LoadType::Full)
    {
        Clear();

        _terrainRenderer->Reserve(numChunks, reserveOffsets);
        _chunkIDToLoadedID.reserve(numChunks);
        _chunkIDToBodyID.reserve(numChunks);
    }
}
