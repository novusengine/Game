#include "ModelLoader.h"
#include "ModelBuilder.h"
#include "ModelRenderer.h"
#include "Game-Lib/Application/EnttRegistries.h"
#include "Game-Lib/ECS/Singletons/Database/ClientDBSingleton.h"
#include "Game-Lib/ECS/Singletons/Database/ItemSingleton.h"
#include "Game-Lib/ECS/Singletons/JoltState.h"
#include "Game-Lib/ECS/Components/AnimationData.h"
#include "Game-Lib/ECS/Components/Events.h"
#include "Game-Lib/ECS/Components/Model.h"
#include "Game-Lib/ECS/Components/Name.h"
#include "Game-Lib/ECS/Components/Unit.h"
#include "Game-Lib/ECS/Singletons/Skybox.h"
#include "Game-Lib/ECS/Util/EventUtil.h"
#include "Game-Lib/ECS/Util/Transforms.h"
#include "Game-Lib/ECS/Util/Database/ItemUtil.h"
#include "Game-Lib/Gameplay/MapLoader.h"
#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Rendering/Debug/DebugRenderer.h"
#include "Game-Lib/Rendering/Light/LightRenderer.h"
#include "Game-Lib/Rendering/Terrain/TerrainLoader.h"
#include "Game-Lib/Util/AssetPath.h"
#include "Game-Lib/Util/JoltStream.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Base/CVarSystem/CVarSystem.h>
#include <Base/Memory/FileReader.h>
#include <Base/Util/StringUtils.h>

#include <FileFormat/Novus/Map/Map.h>
#include <FileFormat/Novus/Map/MapChunk.h>

#include <Filesystem/PactStorage.h>

#include <MetaGen/Shared/ClientDB/ClientDB.h>

#include <entt/entt.hpp>

#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/ScaledShape.h>

#include <xxhash/xxhash64.h>

#include <tracy/Tracy.hpp>

#include <algorithm>
#include <atomic>
#include <execution>
#include <filesystem>
#include <mutex>
#include <vector>

namespace fs = std::filesystem;

static const fs::path dataPath = fs::path("Data/");
static const fs::path complexModelPath = dataPath / "ComplexModel";

AutoCVar_Int CVAR_ModelAsyncPrepare(CVarCategory::Client | CVarCategory::Rendering, "modelAsyncPrepare", "prepare model CPU data on EnkiTS workers", 0, CVarFlags::EditCheckbox);
AutoCVar_Int CVAR_ModelAsyncMaxInFlight(CVarCategory::Client | CVarCategory::Rendering, "modelAsyncMaxInFlight", "maximum in-flight model preparation jobs", 8, CVarFlags::None);
AutoCVar_Int CVAR_ModelAsyncMaxCommitsPerFrame(CVarCategory::Client | CVarCategory::Rendering, "modelAsyncMaxCommitsPerFrame", "maximum prepared models committed per frame", 8, CVarFlags::None);
AutoCVar_Int CVAR_ModelAsyncCommitBudgetMB(CVarCategory::Client | CVarCategory::Rendering, "modelAsyncCommitBudgetMB", "estimated prepared model bytes committed per frame", 32, CVarFlags::None);

struct ActiveModelPrepareJob : enki::ITaskSet
{
public:
    ActiveModelPrepareJob(u64 epoch, u64 modelHash, PACT::PactStorage* pactStorage, moodycamel::ConcurrentQueue<ModelLoading::PreparedModelResult>* completionQueue)
        : enki::ITaskSet(1)
        , _epoch(epoch)
        , _modelHash(modelHash)
        , _pactStorage(pactStorage)
        , _completionQueue(completionQueue)
    {
        m_Priority = enki::TASK_PRIORITY_LOW;
    }

    void ExecuteRange(enki::TaskSetPartition range, u32 threadNum) override
    {
        (void)range;
        (void)threadNum;

        ZoneScopedN("Prepare Model Task");

        ModelLoading::PreparedModelResult result;
        result.epoch = _epoch;
        result.modelHash = _modelHash;
        result.debugName = std::to_string(_modelHash);

        PACT::PactFileHandle fileHandle;
        std::string modelPath;
        {
            ZoneScopedN("Read Model From PACT");
            if (_pactStorage->ReadFile(_modelHash, fileHandle, modelPath) != PACT::PactReadResult::Success)
            {
                result.error = "failed to read model data from PACT";
                _completionQueue->enqueue(std::move(result));
                return;
            }
        }
        result.debugName = std::move(modelPath);

        constexpr size_t HEADER_SIZE = sizeof(FileHeader) + sizeof(Model::ComplexModel::ModelHeader);
        if (fileHandle.GetSize() < HEADER_SIZE)
        {
            result.error = "model file is smaller than its required headers";
            _completionQueue->enqueue(std::move(result));
            return;
        }

        {
            ZoneScopedN("Parse Complex Model");

            std::unique_ptr<Model::ComplexModel> model = std::make_unique<Model::ComplexModel>();
            std::shared_ptr<Bytebuffer> buffer = std::make_shared<Bytebuffer>(const_cast<void*>(fileHandle.GetData()), fileHandle.GetSize());
            buffer->writtenData = fileHandle.GetSize();

            if (!Model::ComplexModel::Read(buffer, *model))
            {
                result.error = "failed to parse model data";
                _completionQueue->enqueue(std::move(result));
                return;
            }

            result.model = std::move(model);
        }

        {
            ZoneScopedN("Build Prepared Render Model");

            ModelLoading::ModelBuildResult buildResult = ModelLoading::BuildPreparedModel(result.debugName, *result.model);
            if (!buildResult)
            {
                result.error = std::move(buildResult.error);
                _completionQueue->enqueue(std::move(result));
                return;
            }

            result.preparedModel = std::move(buildResult.preparedModel);
        }

        _completionQueue->enqueue(std::move(result));
    }

private:
    u64 _epoch = 0;
    u64 _modelHash = std::numeric_limits<u64>().max();
    PACT::PactStorage* _pactStorage = nullptr;
    moodycamel::ConcurrentQueue<ModelLoading::PreparedModelResult>* _completionQueue = nullptr;
};

ModelLoader::ModelLoader(ModelRenderer* modelRenderer, LightRenderer* lightRenderer)
    : _terrainLoader(nullptr)
    , _modelRenderer(modelRenderer)
    , _lightRenderer(lightRenderer)
    , _pendingLoadRequests(MAX_MAP_LOADS_PER_FRAME)
    , _internalLoadRequests(MAX_MAP_LOADS_PER_FRAME)
    , _discoveredModels()
{
    ZoneScoped;

    _pendingLoadRequestsVector.resize(MAX_MAP_LOADS_PER_FRAME);
    _internalLoadRequestsVector.resize(MAX_MAP_LOADS_PER_FRAME);

    _createdEntities.reserve(16384);
    _modelHashToJoltShape.reserve(16384);
    _uniqueIDToinstanceID.reserve(524288);
    _instanceIDToModelID.reserve(524288);
    _instanceIDToBodyID.reserve(524288);
    _instanceIDToEntityID.reserve(524288);
    _entityToLatestRequestID.reserve(16384);
}

void ModelLoader::Init()
{
    ZoneScoped;
}

void ModelLoader::Shutdown()
{
    ZoneScopedN("ModelLoader::Shutdown");

    _loaderEpoch++;
    CancelAndDrainPrepareJobs();
}

void ModelLoader::Clear()
{
    ZoneScopedN("ModelLoader::Clear");

    _loaderEpoch++;
    CancelAndDrainPrepareJobs();

    LoadRequestInternal dummyRequest;
    while (_pendingTerrainLoadRequests.try_dequeue(dummyRequest))
    {
        // Just empty the queue
    }
    while (_pendingLoadRequests.try_dequeue(dummyRequest))
    {
        // Just empty the queue
    }

    while (_internalLoadRequests.try_dequeue(dummyRequest))
    {
        // Just empty the queue
    }

    LoadRequestResultInternal dummyResult;
    while (_loadRequestResults.try_dequeue(dummyResult))
    {
        // Just empty the queue
    }

    UnloadRequest dummyUnloadRequest;
    while (_unloadRequests.try_dequeue(dummyUnloadRequest))
    {
        // Just empty the queue
    }

    for (auto& pair : _modelAssets)
    {
        pair.second.waitingRequests.clear();
        if (pair.second.loadState != LoadState::Failed)
            pair.second.loadState = LoadState::NotLoaded;
    }
    _modelHashToModelID.clear();

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;

    u32 numInstanceIDToBodyIDs = static_cast<u32>(_instanceIDToBodyID.size());
    if (numInstanceIDToBodyIDs > 0)
    {
        auto& joltState = registry->ctx().get<ECS::Singletons::JoltState>();
        JPH::BodyInterface& bodyInterface = joltState.physicsSystem.GetBodyInterface();

        u32 numBodies = static_cast<u32>(_instanceIDToBodyID.size());
        std::vector<JPH::BodyID> bodyIDs(numBodies);

        u32 bodyIndex = 0;
        for (auto& pair : _instanceIDToBodyID)
        {
            JPH::BodyID id = static_cast<JPH::BodyID>(pair.second);
            bodyIDs[bodyIndex++] = id;
        }

        bodyInterface.RemoveBodies(bodyIDs.data(), numBodies);
        bodyInterface.DestroyBodies(bodyIDs.data(), numBodies);

        _instanceIDToBodyID.clear();
    }

    for (auto& pair : _modelHashToJoltShape)
    {
        pair.second = nullptr;
    }
    _modelHashToJoltShape.clear();

    _uniqueIDToinstanceID.clear();
    _instanceIDToModelID.clear();
    _modelIDToModelHash.clear();
    _entityToLatestRequestID.clear();

    _numTerrainModelsToLoad = 0;
    _numTerrainModelsLoaded = 0;
    _modelRenderer->Clear();
    _lightRenderer->Clear();

    auto& tSystem = ECS::TransformSystem::Get(*registry);
    tSystem.ProcessMovedEntities([](entt::entity entity) {});

    registry->destroy(_createdEntities.begin(), _createdEntities.end());
    _createdEntities.clear();
}

void ModelLoader::Update(f32 deltaTime)
{
    ZoneScopedN("ModelLoader::Update");

    if (_terrainLoader->IsLoading())
        return;

    TracyPlot("Model Load Requests Pending", static_cast<i64>(_pendingLoadRequests.size_approx() + _pendingTerrainLoadRequests.size_approx()));
    TracyPlot("Model Instance Requests Pending", static_cast<i64>(_internalLoadRequests.size_approx()));
    TracyPlot("Model Unload Requests Pending", static_cast<i64>(_unloadRequests.size_approx()));
    TracyPlot("Model Load Results Pending", static_cast<i64>(_loadRequestResults.size_approx()));
    TracyPlot("Model Prepare Results Pending", static_cast<i64>(_preparedModelResults.size_approx()));

    ReapCompletedPrepareJobs();
    ConsumePreparedModels();

    TracyPlot("Model Prepare In Flight", static_cast<i64>(_activeModelPrepareJobs.size()));
    TracyPlot("Model Prepare Commit Backlog", static_cast<i64>(_pendingPreparedModelCommits.size() + _preparedModelResults.size_approx()));

    const bool asyncPrepareEnabled = CVAR_ModelAsyncPrepare.Get() != 0;
    const bool drainingDisabledAsyncWork = !asyncPrepareEnabled && (!_activeModelPrepareJobs.empty() || !_pendingPreparedModelCommits.empty());
    u32 numTerrainLoadRequests = static_cast<u32>(_pendingTerrainLoadRequests.size_approx());
    moodycamel::ConcurrentQueue<LoadRequestInternal>* workQueue = numTerrainLoadRequests > 0 ? &_pendingTerrainLoadRequests : &_pendingLoadRequests;
    const u32 maxLoadsThisFrame = _terrainLoading ? MAX_MAP_LOADS_PER_FRAME : MAX_GAMEPLAY_LOADS_PER_FRAME;

    u32 numDequeuedLoadRequests = drainingDisabledAsyncWork ? 0 : static_cast<u32>(workQueue->try_dequeue_bulk(_pendingLoadRequestsVector.data(), maxLoadsThisFrame));
    TracyPlot("Model Load Requests Dequeued", static_cast<i64>(numDequeuedLoadRequests));
    if (asyncPrepareEnabled)
    {
        DispatchAsyncLoadRequests(*workQueue, numDequeuedLoadRequests);
    }
    else if (numDequeuedLoadRequests > 0)
    {
        if (numTerrainLoadRequests > 0)
            _numTerrainModelsLoaded += numDequeuedLoadRequests;

        ZoneScopedN("Pending Load Model Requests");
        ModelRenderer::ReserveInfo reserveInfo;

        {
            ZoneScopedN("Calculate Model Reserve Info");

            for (u32 i = 0; i < numDequeuedLoadRequests; i++)
            {
                const LoadRequestInternal& loadRequest = _pendingLoadRequestsVector[i];
                if (!_modelHashToDiscoveredModel.contains(loadRequest.modelHash))
                {
                    auto* pactStorage = ServiceLocator::GetPactStorage();

                    PACT::PactFileHandle fileHandle;
                    if (pactStorage->ReadFile(loadRequest.modelHash, fileHandle) != PACT::PactReadResult::Success)
                        continue;

                    const std::string* modelPath = pactStorage->GetFilePath(loadRequest.modelHash);

                    size_t fileSize = fileHandle.GetSize();
                    constexpr u32 HEADER_SIZE = sizeof(FileHeader) + sizeof(Model::ComplexModel::ModelHeader);

                    if (fileSize < HEADER_SIZE)
                    {
                        NC_LOG_ERROR("ModelLoader : Tried to open model file ({0}) but it was smaller than sizeof(FileHeader) + sizeof(ModelHeader)", *modelPath);

                        _modelAssets[loadRequest.modelHash].loadState = LoadState::Failed;

                        continue;
                    }

                    DiscoveredModel newDiscoveredModel =
                    {
                        .name = *modelPath,
                        .modelHash = loadRequest.modelHash,
                        .hasShape = false,
                        .model = nullptr
                    };

                    {
                        ZoneScopedN("Read Model");

                        Model::ComplexModel* model = new Model::ComplexModel();
                        std::shared_ptr<Bytebuffer> buffer = std::make_shared<Bytebuffer>(const_cast<void*>(fileHandle.GetData()), fileHandle.GetSize());
                        buffer->writtenData = fileHandle.GetSize();

                        if (!Model::ComplexModel::Read(buffer, *model))
                        {
                            NC_LOG_ERROR("ModelLoader : Failed to read the Model for file ({0})", newDiscoveredModel.name);
                            delete model;

                            _modelAssets[loadRequest.modelHash].loadState = LoadState::Failed;

                            continue;
                        }

                        newDiscoveredModel.model = model;
                        _modelAssets[newDiscoveredModel.modelHash].loadState = LoadState::NotLoaded;
                        _modelHashToDiscoveredModel[newDiscoveredModel.modelHash] = std::move(newDiscoveredModel);
                    }

                    DiscoveredModel& discoveredModel = _modelHashToDiscoveredModel[loadRequest.modelHash];
                    bool isSupported = discoveredModel.model->modelHeader.numVertices > 0;

                    reserveInfo.numModels += 1 * isSupported;
                    reserveInfo.numVertices += discoveredModel.model->modelHeader.numVertices * isSupported;
                    reserveInfo.numIndices += discoveredModel.model->modelHeader.numIndices * isSupported;
                    reserveInfo.numTextureUnits += discoveredModel.model->modelHeader.numTextureUnits * isSupported;
                    reserveInfo.numDecorationSets += discoveredModel.model->modelHeader.numDecorationSets * isSupported;
                    reserveInfo.numDecorations += discoveredModel.model->modelHeader.numDecorations * isSupported;

                    reserveInfo.numOpaqueDrawcalls += discoveredModel.model->modelHeader.numOpaqueRenderBatches * isSupported;
                    reserveInfo.numTransparentDrawcalls += discoveredModel.model->modelHeader.numTransparentRenderBatches * isSupported;
                }
            }
        }

        {
            ZoneScopedN("Model Reserve");

            _modelHashToModelID.reserve(_modelHashToModelID.size() + reserveInfo.numModels);
            _modelIDToModelHash.reserve(_modelIDToModelHash.size() + reserveInfo.numModels);
            _modelIDToAABB.reserve(_modelIDToAABB.size() + reserveInfo.numModels);
            _modelIDToAABB.reserve(_modelIDToAABB.size() + reserveInfo.numModels);
            _modelAssets.reserve(_modelAssets.size() + reserveInfo.numModels);

            _modelRenderer->Reserve(reserveInfo);
        }

        //enki::TaskSet loadModelsTask(numDequeuedLoadRequests, [&](enki::TaskSetPartition range, uint32_t threadNum)
        //{
            //ZoneScopedN("Load Model Task");
            //for (u32 i = range.start; i < range.end; i++)
        {
            ZoneScopedN("Load Models");

            for (u32 i = 0; i < numDequeuedLoadRequests; i++)
            {
                ZoneScopedN("Load Model Request");

                const LoadRequestInternal& loadRequest = _pendingLoadRequestsVector[i];
                bool isStatic = loadRequest.type == LoadRequestType::Placement || loadRequest.type == LoadRequestType::Decoration;

                if (!_modelHashToDiscoveredModel.contains(loadRequest.modelHash))
                {
                    EnqueueLoadResult(loadRequest, false, isStatic);
                    continue;
                }

                ModelAssetRecord& asset = _modelAssets[loadRequest.modelHash];
                LoadState loadState = asset.loadState;
                if (loadState == LoadState::NotLoaded)
                {
                    ZoneScopedN("Load Model Into Renderer");
                    DiscoveredModel& discoveredModel = _modelHashToDiscoveredModel[loadRequest.modelHash];

                    bool didLoad = LoadRequest(discoveredModel);
                    loadState = static_cast<LoadState>((LoadState::Loaded * didLoad) + (LoadState::Failed * !didLoad));
                    asset.loadState = loadState;
                }

                if (loadState == LoadState::Failed)
                {
                    EnqueueLoadResult(loadRequest, false, isStatic);
                    continue;
                }

                _internalLoadRequests.enqueue(loadRequest);
            }
        }
        //});

        //taskScheduler->AddTaskSetToPipe(&loadModelsTask);
        //taskScheduler->WaitforTask(&loadModelsTask);
    }

    u32 numDequeuedInternalRequests = static_cast<u32>(_internalLoadRequests.try_dequeue_bulk(_internalLoadRequestsVector.data(), maxLoadsThisFrame));
    TracyPlot("Model Instance Requests Dequeued", static_cast<i64>(numDequeuedInternalRequests));
    u32 numStaticInstancesCommitted = 0;
    u32 numDynamicInstancesCommitted = 0;
    if (numDequeuedInternalRequests > 0)
    {
        ZoneScopedN("Pending Load Instance Requests");

        ModelRenderer::ReserveInfo reserveInfo;

        {
            ZoneScopedN("Calculate Instance Reserve Info");

            for (u32 i = 0; i < numDequeuedInternalRequests; i++)
            {
                ZoneScopedN("Reserve Info Work");
                const LoadRequestInternal& loadRequest = _internalLoadRequestsVector[i];

                u64 modelHash = loadRequest.modelHash;
                const DiscoveredModel& discoveredModel = _modelHashToDiscoveredModel[modelHash];
                bool isSupported = discoveredModel.model->modelHeader.numVertices > 0;
                bool hasDisplayID = loadRequest.type == LoadRequestType::DisplayID;

                // Only increment Instance Count & Drawcall Count if the model have vertices
                {
                    reserveInfo.numInstances += 1 * isSupported;
                    reserveInfo.numOpaqueDrawcalls += discoveredModel.model->modelHeader.numOpaqueRenderBatches * isSupported;
                    reserveInfo.numTransparentDrawcalls += discoveredModel.model->modelHeader.numTransparentRenderBatches * isSupported;
                    reserveInfo.numBones += discoveredModel.model->modelHeader.numBones * isSupported;
                    reserveInfo.numTextureTransforms += discoveredModel.model->modelHeader.numTextureTransforms * isSupported;
                    reserveInfo.numTextureUnits += discoveredModel.model->modelHeader.numTextureUnits * hasDisplayID * isSupported;
                }
            }
        }

        if (reserveInfo.numInstances > 0)
        {
            entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
            size_t createdEntitiesOffset = _createdEntities.size();

            {
                ZoneScopedN("Instance Reserve");

                // Create entt entities
                _createdEntities.resize(createdEntitiesOffset + reserveInfo.numInstances);
                auto begin = _createdEntities.begin() + createdEntitiesOffset;

                registry->create(begin, _createdEntities.end());

                registry->insert<ECS::Components::AABB>(begin, _createdEntities.end());
                registry->insert<ECS::Components::AnimationInitData>(begin, _createdEntities.end());
                registry->insert<ECS::Components::DirtyTransform>(begin, _createdEntities.end());
                registry->insert<ECS::Components::Model>(begin, _createdEntities.end());
                registry->insert<ECS::Components::Name>(begin, _createdEntities.end());
                registry->insert<ECS::Components::Transform>(begin, _createdEntities.end());
                registry->insert<ECS::Components::WorldAABB>(begin, _createdEntities.end());

                _modelRenderer->Reserve(reserveInfo);
            }

            std::atomic<u32> numCreatedInstances = 0;
            //enki::TaskSet loadModelInstancesTask(numDequeuedInternalRequests, [&](enki::TaskSetPartition range, uint32_t threadNum)
            //{
                //ZoneScopedN("Load Instance Task");

                //for (u32 i = range.start; i < range.end; i++)
            {
                ZoneScopedN("Load Instances");

                for (u32 i = 0; i < numDequeuedInternalRequests; i++)
                {
                    ZoneScopedN("Load Instance Work");
                    LoadRequestInternal& loadRequest = _internalLoadRequestsVector[i];

                    const DiscoveredModel& discoveredModel = _modelHashToDiscoveredModel[loadRequest.modelHash];
                    bool isSupported = (loadRequest.entity == entt::null || registry->valid(loadRequest.entity)) && discoveredModel.model->modelHeader.numVertices > 0;
                    if (!IsCurrentEntityRequest(loadRequest))
                        continue;

                    if (!isSupported)
                        continue;

                    switch (loadRequest.type)
                    {
                        case LoadRequestType::Placement:
                        case LoadRequestType::Decoration:
                        {
                            ZoneScopedN("Load Placement / Decoration");

                            bool hasUniqueID = loadRequest.extraData3 != std::numeric_limits<u64>().max();
                            bool uniqueIDExists = _uniqueIDToinstanceID.contains(static_cast<u32>(loadRequest.extraData3));

                            if (hasUniqueID && uniqueIDExists)
                                break;

                            if (loadRequest.entity == entt::null)
                            {
                                u32 index = static_cast<u32>(createdEntitiesOffset) + numCreatedInstances.fetch_add(1);
                                loadRequest.entity = _createdEntities[index];
                            }

                            AddStaticInstance(loadRequest.entity, loadRequest);
                            numStaticInstancesCommitted++;
                            EnqueueLoadResult(loadRequest, true, true);
                            break;
                        }

                        case LoadRequestType::Model:
                        case LoadRequestType::DisplayID:
                        {
                            ZoneScopedN("Load Model / DisplayID");
                            if (loadRequest.entity == entt::null)
                            {
                                u32 index = static_cast<u32>(createdEntitiesOffset) + numCreatedInstances.fetch_add(1);
                                loadRequest.entity = _createdEntities[index];
                            }

                            AddDynamicInstance(loadRequest.entity, loadRequest);
                            numDynamicInstancesCommitted++;
                            EnqueueLoadResult(loadRequest, true, false);
                            break;
                        }

                        default: break;
                    }
                }
            }

            //});

            //taskScheduler->AddTaskSetToPipe(&loadModelInstancesTask);
            //taskScheduler->WaitforTask(&loadModelInstancesTask);

            // Destroy the entities we didn't use
            u32 numCreated = numCreatedInstances.load();
            auto begin = _createdEntities.begin() + createdEntitiesOffset;

            registry->destroy(begin + numCreated, _createdEntities.end());
            _createdEntities.resize(createdEntitiesOffset + numCreated);
        }
    }
    TracyPlot("Model Static Instances Committed", static_cast<i64>(numStaticInstancesCommitted));
    TracyPlot("Model Dynamic Instances Committed", static_cast<i64>(numDynamicInstancesCommitted));

    size_t unloadRequests = _unloadRequests.size_approx();
    u32 numUnloadsProcessed = 0;
    if (unloadRequests)
    {
        ZoneScopedN("Unload Requests Task");

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& joltState = registry->ctx().get<ECS::Singletons::JoltState>();
        JPH::BodyInterface& bodyInterface = joltState.physicsSystem.GetBodyInterface();

        UnloadRequest unloadRequest;
        while (_unloadRequests.try_dequeue(unloadRequest))
        {
            numUnloadsProcessed++;
            if (_instanceIDToModelID.contains(unloadRequest.instanceID))
                _instanceIDToModelID.erase(unloadRequest.instanceID);

            if (_instanceIDToBodyID.contains(unloadRequest.instanceID))
            {
                if (registry->valid(unloadRequest.entity))
                {
                    if (auto* unit = registry->try_get<ECS::Components::Unit>(unloadRequest.entity))
                    {
                        unit->bodyID = std::numeric_limits<u32>().max();
                    }
                }

                JPH::BodyID bodyID = static_cast<JPH::BodyID>(_instanceIDToBodyID[unloadRequest.instanceID]);

                bodyInterface.RemoveBody(bodyID);
                bodyInterface.DestroyBody(bodyID);

                _instanceIDToBodyID.erase(unloadRequest.instanceID);
            }

            if (_instanceIDToEntityID.contains(unloadRequest.instanceID))
                _instanceIDToEntityID.erase(unloadRequest.instanceID);

            _modelRenderer->RemoveInstance(unloadRequest.instanceID);
        }
    }
    TracyPlot("Model Unloads Processed", static_cast<i64>(numUnloadsProcessed));

    {
        ZoneScopedN("Load Results");

        u32 numResultsProcessed = 0;
        LoadRequestResultInternal loadRequestResult;
        while (_loadRequestResults.try_dequeue(loadRequestResult))
        {
            numResultsProcessed++;
            ZoneScopedN("Load Result Work");
            const LoadRequestInternal& loadRequest = loadRequestResult.request;
            NC_ASSERT(loadRequest.requestID == loadRequestResult.requestID, "ModelLoader load result request ID does not match its request snapshot");
            if (!IsCurrentEntityRequest(loadRequest))
                continue;

            if (loadRequest.entity == entt::null)
                continue;

            entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
            if (!registry->valid(loadRequest.entity) || !registry->all_of<ECS::Components::Model>(loadRequest.entity))
                continue;

            auto& model = registry->get<ECS::Components::Model>(loadRequest.entity);
            model.flags.loaded = loadRequestResult.success;

            // Rollback to previous ModelHash if load failed (Defaults to Invalid if no prior model hash was present)
            bool rollback = false;
            if (!loadRequestResult.success)
            {
                model.flags.loaded = false;

                rollback = loadRequest.extraData2 != std::numeric_limits<u64>().max();
                model.modelHash = loadRequest.extraData2;
            }

            ECS::Components::ModelLoadedEvent modelLoadedEvent = {};
            modelLoadedEvent.flags.loaded = loadRequestResult.success;
            modelLoadedEvent.flags.rollback = rollback;
            modelLoadedEvent.flags.staticModel = loadRequestResult.isStatic;

            ECS::Util::EventUtil::PushEventTo(*registry, loadRequest.entity, std::move(modelLoadedEvent));
        }
        TracyPlot("Model Load Results Processed", static_cast<i64>(numResultsProcessed));
    }


    const bool modelPreparationIdle = _activeModelPrepareJobs.empty() && _preparedModelResults.size_approx() == 0 && _pendingPreparedModelCommits.empty();
    const bool terrainRequestQueuesIdle = _pendingTerrainLoadRequests.size_approx() == 0 && _internalLoadRequests.size_approx() == 0;
    if (_terrainLoading && _numTerrainModelsLoaded == _numTerrainModelsToLoad && modelPreparationIdle && terrainRequestQueuesIdle)
    {
        SetTerrainLoading(false);

        u32 mapID = ServiceLocator::GetGameRenderer()->GetMapLoader()->GetCurrentMapID();
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& joltState = registry->ctx().get<ECS::Singletons::JoltState>();
        joltState.LogPhysicsTelemetrySummary("Map Loaded");

        ECS::Util::EventUtil::PushEvent(ECS::Components::MapLoadedEvent{ mapID });
    }
}

entt::entity ModelLoader::CreateModelEntity(const std::string& name)
{
    ZoneScopedN("ModelLoader::CreateModelEntity");

    entt::registry& registry = *ServiceLocator::GetEnttRegistries()->gameRegistry;

    entt::entity entity = registry.create();
    auto& nameComponent = registry.emplace<ECS::Components::Name>(entity);
    nameComponent.name = name;
    nameComponent.nameHash = StringUtils::fnv1a_32(name.c_str(), name.size());
    nameComponent.fullName = "";

    registry.emplace<ECS::Components::AABB>(entity);
    registry.emplace<ECS::Components::Transform>(entity);
    registry.emplace<ECS::Components::Model>(entity);

    return entity;
}

f32 ModelLoader::GetLoadingProgress() const
{
    u32 numModelsToLoad = _numTerrainModelsToLoad;
    u32 min = _numTerrainModelsLoaded;
    u32 max = glm::max(1u, numModelsToLoad);

    f32 terrainModelProgress = static_cast<f32>(min) / static_cast<f32>(max);
    return terrainModelProgress;
}

void ModelLoader::LoadPlacement(const Terrain::Placement& placement)
{
    ZoneScopedN("ModelLoader::LoadPlacement");

    LoadRequestInternal loadRequest;

    loadRequest.requestID = GetNextLoadRequestID();
    loadRequest.type = LoadRequestType::Placement;
    loadRequest.entity = entt::null;
    loadRequest.modelHash = placement.nameHash;

    loadRequest.spawnPosition = placement.position;
    loadRequest.spawnRotation = placement.rotation;
    loadRequest.scale = static_cast<f32>(placement.scale) / 1024.0f;

    bool isDoodadSetPresent = placement.doodadSet != std::numeric_limits<u16>().max();
    u64 doodadSet = (placement.doodadSet * isDoodadSetPresent) + (std::numeric_limits<u64>().max() * !isDoodadSetPresent);
    loadRequest.extraData2 = doodadSet;
    loadRequest.extraData3 = placement.uniqueID;

    _numTerrainModelsToLoad++;
    _pendingTerrainLoadRequests.enqueue(loadRequest);
}

void ModelLoader::LoadDecoration(u32 instanceID, const Model::ComplexModel::Decoration& decoration)
{
    ZoneScopedN("ModelLoader::LoadDecoration");

    LoadRequestInternal loadRequest =
    {
        .requestID = GetNextLoadRequestID(),
        .type = LoadRequestType::Decoration,
        .entity = entt::null,
        .modelHash = decoration.nameID,
        .spawnPosition = decoration.position,
        .spawnRotation = decoration.rotation,
        .scale = decoration.scale,
        .extraData1 = instanceID,
        .extraData2 = decoration.color,
    };

    _numTerrainModelsToLoad++;
    _pendingTerrainLoadRequests.enqueue(loadRequest);
}

bool ModelLoader::LoadModelForEntity(entt::entity entity, ECS::Components::Model& model, u64 modelNameHash)
{
    ZoneScopedN("ModelLoader::LoadModelForEntity");

    auto* pactStorage = ServiceLocator::GetPactStorage();
    if (!pactStorage->FileExists(modelNameHash))
        return false;

    LoadRequestInternal loadRequest;
    loadRequest.requestID = GetNextLoadRequestID();
    loadRequest.type = LoadRequestType::Model;
    loadRequest.entity = entity;
    loadRequest.modelHash = modelNameHash;
    loadRequest.extraData2 = model.modelHash;
    _entityToLatestRequestID[entt::to_integral(entity)] = loadRequest.requestID;

    model.flags.loaded = false;
    model.modelHash = modelNameHash;

    _pendingLoadRequests.enqueue(loadRequest);

    return true;
}

bool ModelLoader::LoadDisplayIDForEntity(entt::entity entity, ECS::Components::Model& model, Database::Unit::DisplayInfoType displayInfoType, u32 displayID, u64 modelHash, u8 modelVariant)
{
    ZoneScopedN("ModelLoader::LoadDisplayIDForEntity");

    entt::registry* gameRegistry = ServiceLocator::GetEnttRegistries()->gameRegistry;
    entt::registry* dbRegistry = ServiceLocator::GetEnttRegistries()->dbRegistry;
    auto& clientDBSingleton = dbRegistry->ctx().get<ECS::Singletons::ClientDBSingleton>();

    f32 scale = 1.0f;
    u32 extendedDisplayInfoID = 0;
    bool isDynamicModel = false;

    if (modelHash == std::numeric_limits<u64>().max())
    {
        switch (displayInfoType)
        {
            case Database::Unit::DisplayInfoType::Creature:
            {
                auto* creatureDisplayInfoStorage = clientDBSingleton.Get(ClientDBHash::CreatureDisplayInfo);
                auto* creatureModelDataStorage = clientDBSingleton.Get(ClientDBHash::CreatureModelData);

                if (!creatureDisplayInfoStorage->Has(displayID))
                    return false;

                const auto& creatureDisplayInfo = creatureDisplayInfoStorage->Get<MetaGen::Shared::ClientDB::CreatureDisplayInfoRecord>(displayID);
                const auto& creatureModelData = creatureModelDataStorage->Get<MetaGen::Shared::ClientDB::CreatureModelDataRecord>(creatureDisplayInfo.modelID);

                modelHash = creatureModelDataStorage->GetStringHash(creatureModelData.model);
                scale = creatureDisplayInfo.creatureModelScale;
                extendedDisplayInfoID = creatureDisplayInfo.extendedDisplayInfoID;
                isDynamicModel = (creatureModelData.flags & 0x4) != 0x0; // Model is flagged as Dynamic
                break;
            }

            case Database::Unit::DisplayInfoType::GameObject:
            {
                break;
            }

            case Database::Unit::DisplayInfoType::Item:
            {
                auto& itemSingleton = dbRegistry->ctx().get<ECS::Singletons::ItemSingleton>();
                auto* modelFileDataStorage = clientDBSingleton.Get(ClientDBHash::ModelFileData);
                auto* itemDisplayInfoStorage = clientDBSingleton.Get(ClientDBHash::ItemDisplayInfo);

                if (!itemDisplayInfoStorage->Has(displayID))
                    return false;

                const auto& itemDisplayInfo = itemDisplayInfoStorage->Get<MetaGen::Shared::ClientDB::ItemDisplayInfoRecord>(displayID);
                u64 itemModelHash = std::numeric_limits<u64>().max();
                u32 modelResourcesID = itemDisplayInfo.modelResourcesID[0];

                // TODO : This is a hack to bypass a lookup table for now. A lookup table is needed so that we can go from modelResourcesID -> List<ModelFileData>
                modelFileDataStorage->Each([&modelFileDataStorage, &itemModelHash, modelResourcesID](u32 id, const MetaGen::Shared::ClientDB::ModelFileDataRecord& modelFileData) -> bool
                {
                    if (modelFileData.modelResourcesID != modelResourcesID)
                        return true;

                    itemModelHash = modelFileDataStorage->GetStringHash(modelFileData.model);
                    return false;
                });

                modelHash = itemModelHash;
                break;
            }

            default: break;
        }
    }

    auto* pactStorage = ServiceLocator::GetPactStorage();
    if (!pactStorage->FileExists(modelHash))
        return false;

    LoadRequestInternal loadRequest;
    loadRequest.requestID = GetNextLoadRequestID();
    loadRequest.type = LoadRequestType::DisplayID;
    loadRequest.entity = entity;
    loadRequest.modelHash = modelHash;
    loadRequest.scale = scale;

    u64 displayInfoPacked = displayID | (static_cast<u64>(displayInfoType) & 0x7) << 32 | (static_cast<u64>(modelVariant) & 0x3F) << 35 | (static_cast<u64>(extendedDisplayInfoID) << 41) | (static_cast<u64>(isDynamicModel) << 63);
    loadRequest.extraData1 = displayInfoPacked;
    loadRequest.extraData2 = model.modelHash;
    _entityToLatestRequestID[entt::to_integral(entity)] = loadRequest.requestID;
    model.flags.loaded = false;
    model.modelHash = modelHash;

    _pendingLoadRequests.enqueue(loadRequest);
    return true;
}

void ModelLoader::UnloadModelForEntity(entt::entity entity, ECS::Components::Model& model)
{
    ZoneScopedN("ModelLoader::UnloadModelForEntity");

    if (model.instanceID == std::numeric_limits<u32>().max())
        return;

    UnloadRequest unloadRequest =
    {
        .entity = entity,
        .instanceID = model.instanceID
    };

    model.flags.loaded = false;
    model.modelHash = std::numeric_limits<u64>().max();
    _entityToLatestRequestID.erase(entt::to_integral(entity));

    _unloadRequests.enqueue(unloadRequest);
}

void ModelLoader::SetEntityVisible(entt::entity entity, bool visible)
{
    ZoneScopedN("ModelLoader::SetEntityVisible");

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
    auto& modelComponent = registry->get<ECS::Components::Model>(entity);

    SetModelVisible(modelComponent, visible);
}

void ModelLoader::SetModelVisible(const ECS::Components::Model& model, bool visible)
{
    ZoneScopedN("ModelLoader::SetModelVisible");

    if (model.instanceID == std::numeric_limits<u32>().max())
        return;

    _modelRenderer->RequestChangeVisibility(model.instanceID, visible);
}

void ModelLoader::SetEntityTransparent(entt::entity entity, bool transparent, f32 opacity)
{
    ZoneScopedN("ModelLoader::SetEntityTransparent");

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
    auto& modelComponent = registry->get<ECS::Components::Model>(entity);

    SetModelTransparent(modelComponent, transparent, opacity);
}

void ModelLoader::SetModelTransparent(const ECS::Components::Model& model, bool transparent, f32 opacity)
{
    ZoneScopedN("ModelLoader::SetModelTransparent");

    if (model.instanceID == std::numeric_limits<u32>().max())
        return;

    _modelRenderer->RequestChangeTransparency(model.instanceID, transparent, opacity);
}

void ModelLoader::SetEntityHighlight(entt::entity entity, f32 highlightIntensity)
{
    ZoneScopedN("ModelLoader::SetEntityHighlight");

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
    auto& modelComponent = registry->get<ECS::Components::Model>(entity);

    SetModelHighlight(modelComponent, highlightIntensity);
}

void ModelLoader::SetModelHighlight(const ECS::Components::Model& model, f32 highlightIntensity)
{
    ZoneScopedN("ModelLoader::SetModelHighlight");

    if (model.instanceID == std::numeric_limits<u32>().max())
        return;

    _modelRenderer->RequestChangeHighlight(model.instanceID, highlightIntensity);
}

void ModelLoader::EnableGroupForEntity(entt::entity entity, u32 groupID)
{
    ZoneScopedN("ModelLoader::EnableGroupForEntity");

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
    auto& modelComponent = registry->get<ECS::Components::Model>(entity);

    EnableGroupForModel(modelComponent, groupID);
}

void ModelLoader::EnableGroupForModel(const ECS::Components::Model& model, u32 groupID)
{
    ZoneScopedN("ModelLoader::EnableGroupForModel");

    if (model.instanceID == std::numeric_limits<u32>().max())
        return;

    _modelRenderer->RequestChangeGroup(model.instanceID, groupID, 0, true);
}

void ModelLoader::DisableGroupForEntity(entt::entity entity, u32 groupID)
{
    ZoneScopedN("ModelLoader::DisableGroupForEntity");

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
    auto& modelComponent = registry->get<ECS::Components::Model>(entity);

    DisableGroupForModel(modelComponent, groupID);
}

void ModelLoader::DisableGroupForModel(const ECS::Components::Model& model, u32 groupID)
{
    ZoneScopedN("ModelLoader::DisableGroupForModel");

    if (model.instanceID == std::numeric_limits<u32>().max())
        return;

    _modelRenderer->RequestChangeGroup(model.instanceID, groupID, 0, false);
}

void ModelLoader::DisableGroupsForEntity(entt::entity entity, u32 groupIDStart, u32 groupIDEnd)
{
    ZoneScopedN("ModelLoader::DisableGroupsForEntity");

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
    auto& modelComponent = registry->get<ECS::Components::Model>(entity);

    DisableGroupsForModel(modelComponent, groupIDStart, groupIDEnd);
}

void ModelLoader::DisableGroupsForModel(const ECS::Components::Model& model, u32 groupIDStart, u32 groupIDEnd)
{
    ZoneScopedN("ModelLoader::DisableGroupsForModel");

    if (model.instanceID == std::numeric_limits<u32>().max())
        return;

    _modelRenderer->RequestChangeGroup(model.instanceID, groupIDStart, groupIDEnd, false);
}

void ModelLoader::DisableAllGroupsForEntity(entt::entity entity)
{
    ZoneScopedN("ModelLoader::DisableAllGroupsForEntity");

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
    auto& modelComponent = registry->get<ECS::Components::Model>(entity);

    DisableAllGroupsForModel(modelComponent);
}

void ModelLoader::DisableAllGroupsForModel(const ECS::Components::Model& model)
{
    ZoneScopedN("ModelLoader::DisableAllGroupsForModel");

    if (model.instanceID == std::numeric_limits<u32>().max())
        return;

    _modelRenderer->RequestChangeGroup(model.instanceID, 1, std::numeric_limits<u32>().max(), false);
}

void ModelLoader::SetSkinTextureForEntity(entt::entity entity, Renderer::TextureID textureID)
{
    ZoneScopedN("ModelLoader::SetSkinTextureForEntity");

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
    auto& modelComponent = registry->get<ECS::Components::Model>(entity);
    SetSkinTextureForModel(modelComponent, textureID);
}

void ModelLoader::SetSkinTextureForModel(const ECS::Components::Model& model, Renderer::TextureID textureID)
{
    ZoneScopedN("ModelLoader::SetSkinTextureForModel");

    if (model.instanceID == std::numeric_limits<u32>().max())
        return;

    _modelRenderer->RequestChangeSkinTexture(model.instanceID, textureID);
}

void ModelLoader::SetHairTextureForEntity(entt::entity entity, Renderer::TextureID textureID)
{
    ZoneScopedN("ModelLoader::SetHairTextureForEntity");

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
    auto& modelComponent = registry->get<ECS::Components::Model>(entity);
    SetHairTextureForModel(modelComponent, textureID);
}

void ModelLoader::SetHairTextureForModel(const ECS::Components::Model& model, Renderer::TextureID textureID)
{
    ZoneScopedN("ModelLoader::SetHairTextureForModel");

    if (model.instanceID == std::numeric_limits<u32>().max())
        return;

    _modelRenderer->RequestChangeHairTexture(model.instanceID, textureID);
}

const Model::ComplexModel* ModelLoader::GetModelInfo(u64 modelHash)
{
    ZoneScopedN("ModelLoader::GetModelInfo");

    if (!_modelHashToDiscoveredModel.contains(modelHash))
        return nullptr;

    const auto& discoveredModel = _modelHashToDiscoveredModel[modelHash];
    return discoveredModel.model;
}

u64 ModelLoader::GetModelHashFromModelPath(const std::string& modelPath)
{
    ZoneScopedN("ModelLoader::GetModelHashFromModelPath");

    return Util::AssetPath::Hash(modelPath);
}

bool ModelLoader::GetModelIDFromInstanceID(u32 instanceID, u32& modelID)
{
    ZoneScopedN("ModelLoader::GetModelIDFromInstanceID");

    if (!_instanceIDToModelID.contains(instanceID))
        return false;

    modelID = _instanceIDToModelID[instanceID];
    return true;
}

bool ModelLoader::GetEntityIDFromInstanceID(u32 instanceID, entt::entity& entityID)
{
    ZoneScopedN("ModelLoader::GetEntityIDFromInstanceID");

    if (!_instanceIDToEntityID.contains(instanceID))
        return false;

    entityID = _instanceIDToEntityID[instanceID];
    return true;
}

bool ModelLoader::GetBodyIDFromInstanceID(u32 instanceID, u32& bodyID)
{
    ZoneScopedN("ModelLoader::GetBodyIDFromInstanceID");

    if (!_instanceIDToBodyID.contains(instanceID))
        return false;

    bodyID = _instanceIDToBodyID[instanceID];
    return true;
}

bool ModelLoader::ContainsDiscoveredModel(u64 modelHash)
{
    ZoneScopedN("ModelLoader::ContainsDiscoveredModel");
    return _modelHashToDiscoveredModel.contains(modelHash);
}

ModelLoader::DiscoveredModel& ModelLoader::GetDiscoveredModel(u64 modelHash)
{
    ZoneScopedN("ModelLoader::GetDiscoveredModel");

    if (!_modelHashToDiscoveredModel.contains(modelHash))
    {
        NC_LOG_CRITICAL("ModelLoader : Tried to access DiscoveredModel of invalid ModelHash {0}", modelHash);
    }

    return _modelHashToDiscoveredModel[modelHash];
}

ModelLoader::DiscoveredModel& ModelLoader::GetDiscoveredModelFromModelID(u32 modelID)
{
    ZoneScopedN("ModelLoader::GetDiscoveredModelFromModelID");

    if (!_modelIDToModelHash.contains(modelID))
    {
        NC_LOG_CRITICAL("ModelLoader : Tried to access DiscoveredModel of invalid ModelID {0}", modelID);
    }

    u64 modelHash = _modelIDToModelHash[modelID];
    if (!_modelHashToDiscoveredModel.contains(modelHash))
    {
        NC_LOG_CRITICAL("ModelLoader : Tried to access DiscoveredModel of invalid ModelHash {0}", modelHash);
    }

    return _modelHashToDiscoveredModel[modelHash];
}

void ModelLoader::ConsumePreparedModels()
{
    ZoneScopedN("ModelLoader::ConsumePreparedModels");

    u32 numResultsConsumed = 0;
    u32 numStaleResultsDiscarded = 0;
    ModelLoading::PreparedModelResult result;
    {
        ZoneScopedN("Drain Prepared Model Results");
        while (_preparedModelResults.try_dequeue(result))
        {
            if (result.epoch != _loaderEpoch)
            {
                numStaleResultsDiscarded++;
                continue;
            }

            numResultsConsumed++;
            _pendingPreparedModelCommits.push_back(std::move(result));
        }
    }
    TracyPlot("Model Prepare Results Consumed", static_cast<i64>(numResultsConsumed));
    TracyPlot("Model Prepare Stale Results", static_cast<i64>(numStaleResultsDiscarded));

    const u32 maxCommits = static_cast<u32>(std::max(1, CVAR_ModelAsyncMaxCommitsPerFrame.Get()));
    const u64 commitBudgetBytes = static_cast<u64>(std::max(1, CVAR_ModelAsyncCommitBudgetMB.Get())) * 1024ull * 1024ull;

    std::vector<ModelLoading::PreparedModelResult> commitBatch;
    commitBatch.reserve(maxCommits);

    u64 selectedCommitBytes = 0;
    {
        ZoneScopedN("Select Prepared Model Commit Batch");
        while (!_pendingPreparedModelCommits.empty() && commitBatch.size() < maxCommits)
        {
            ModelLoading::PreparedModelResult& pendingResult = _pendingPreparedModelCommits.front();

            auto assetItr = _modelAssets.find(pendingResult.modelHash);
            if (assetItr == _modelAssets.end() || assetItr->second.loadState != LoadState::Requested)
            {
                _pendingPreparedModelCommits.pop_front();
                continue;
            }

            if (!pendingResult)
            {
                NC_LOG_ERROR("ModelLoader : Failed to prepare model ({0}): {1}", pendingResult.debugName, pendingResult.error);

                ModelAssetRecord& asset = assetItr->second;
                asset.loadState = LoadState::Failed;
                for (const LoadRequestInternal& request : asset.waitingRequests)
                    CompletePreparedRequest(request, false);
                asset.waitingRequests.clear();

                _pendingPreparedModelCommits.pop_front();
                continue;
            }

            const u64 nextCommitBytes = pendingResult.preparedModel.estimatedCommitBytes;
            if (!commitBatch.empty() && selectedCommitBytes + nextCommitBytes > commitBudgetBytes)
                break;

            selectedCommitBytes += nextCommitBytes;
            commitBatch.push_back(std::move(pendingResult));
            _pendingPreparedModelCommits.pop_front();
        }
    }

    if (commitBatch.empty())
    {
        TracyPlot("Model Prepare Models Committed", static_cast<i64>(0));
        TracyPlot("Model Prepare Committed Bytes", static_cast<i64>(0));
        return;
    }

    TracyPlot("Model Prepare Models Committed", static_cast<i64>(commitBatch.size()));
    TracyPlot("Model Prepare Committed Bytes", static_cast<i64>(selectedCommitBytes));

    ModelRenderer::ReserveInfo reserveInfo;
    {
        ZoneScopedN("Aggregate Prepared Model Reserve");
        for (const ModelLoading::PreparedModelResult& preparedResult : commitBatch)
        {
            reserveInfo.numModels += preparedResult.preparedModel.reserveInfo.numModels;
            reserveInfo.numOpaqueDrawcalls += preparedResult.preparedModel.reserveInfo.numOpaqueDrawCalls;
            reserveInfo.numTransparentDrawcalls += preparedResult.preparedModel.reserveInfo.numTransparentDrawCalls;
            reserveInfo.numVertices += preparedResult.preparedModel.reserveInfo.numVertices;
            reserveInfo.numIndices += preparedResult.preparedModel.reserveInfo.numIndices;
            reserveInfo.numTextureUnits += preparedResult.preparedModel.reserveInfo.numTextureUnits;
            reserveInfo.numBones += preparedResult.preparedModel.reserveInfo.numBones;
            reserveInfo.numTextureTransforms += preparedResult.preparedModel.reserveInfo.numTextureTransforms;
            reserveInfo.numDecorationSets += preparedResult.preparedModel.reserveInfo.numDecorationSets;
            reserveInfo.numDecorations += preparedResult.preparedModel.reserveInfo.numDecorations;
        }
    }

    {
        ZoneScopedN("Reserve Prepared Model Commit");
        _modelHashToModelID.reserve(_modelHashToModelID.size() + commitBatch.size());
        _modelIDToModelHash.reserve(_modelIDToModelHash.size() + commitBatch.size());
        _modelIDToAABB.reserve(_modelIDToAABB.size() + commitBatch.size());
        _modelRenderer->Reserve(reserveInfo);
    }

    for (ModelLoading::PreparedModelResult& preparedResult : commitBatch)
    {
        ZoneScopedN("Commit Prepared Model Result");
        auto assetItr = _modelAssets.find(preparedResult.modelHash);
        if (assetItr == _modelAssets.end() || assetItr->second.loadState != LoadState::Requested)
            continue;

        ModelAssetRecord& asset = assetItr->second;

        if (_modelHashToDiscoveredModel.contains(preparedResult.modelHash))
            delete _modelHashToDiscoveredModel[preparedResult.modelHash].model;

        DiscoveredModel discoveredModel =
        {
            .name = std::move(preparedResult.debugName),
            .modelHash = preparedResult.modelHash,
            .hasShape = false,
            .model = preparedResult.model.release()
        };

        _modelHashToDiscoveredModel[preparedResult.modelHash] = std::move(discoveredModel);
        const bool success = CommitPreparedModel(_modelHashToDiscoveredModel[preparedResult.modelHash], preparedResult.preparedModel);

        asset.loadState = success ? LoadState::Loaded : LoadState::Failed;
        for (const LoadRequestInternal& request : asset.waitingRequests)
            CompletePreparedRequest(request, success);
        asset.waitingRequests.clear();
    }
}

void ModelLoader::ReapCompletedPrepareJobs()
{
    ZoneScopedN("ModelLoader::ReapCompletedPrepareJobs");
    std::erase_if(_activeModelPrepareJobs, [](const std::unique_ptr<ActiveModelPrepareJob>& job)
    {
        return job->GetIsComplete();
    });
}

void ModelLoader::DispatchAsyncLoadRequests(moodycamel::ConcurrentQueue<LoadRequestInternal>& workQueue, u32 numRequests)
{
    ZoneScopedN("ModelLoader::DispatchAsyncLoadRequests");

    enki::TaskScheduler* taskScheduler = ServiceLocator::GetTaskScheduler();
    auto* pactStorage = ServiceLocator::GetPactStorage();
    const u32 maxInFlight = static_cast<u32>(std::max(1, CVAR_ModelAsyncMaxInFlight.Get()));
    u32 numJobsDispatched = 0;
    u32 numRequestsCoalesced = 0;
    u32 numRequestsDeferred = 0;

    for (u32 i = 0; i < numRequests; i++)
    {
        const LoadRequestInternal& request = _pendingLoadRequestsVector[i];
        ModelAssetRecord& asset = _modelAssets[request.modelHash];

        if (asset.loadState == LoadState::Loaded)
        {
            CompletePreparedRequest(request, true);
            continue;
        }

        if (asset.loadState == LoadState::Failed)
        {
            CompletePreparedRequest(request, false);
            continue;
        }

        if (asset.loadState == LoadState::Requested)
        {
            asset.waitingRequests.push_back(request);
            numRequestsCoalesced++;
            continue;
        }

        const size_t numOutstandingPrepares = _activeModelPrepareJobs.size() + _pendingPreparedModelCommits.size() + _preparedModelResults.size_approx();
        if (numOutstandingPrepares >= maxInFlight)
        {
            workQueue.enqueue(request);
            numRequestsDeferred++;
            continue;
        }

        asset.loadState = LoadState::Requested;
        asset.waitingRequests.push_back(request);

        auto job = std::make_unique<ActiveModelPrepareJob>(_loaderEpoch, request.modelHash, pactStorage, &_preparedModelResults);
        taskScheduler->AddTaskSetToPipe(job.get());
        _activeModelPrepareJobs.push_back(std::move(job));
        numJobsDispatched++;
    }

    TracyPlot("Model Prepare Jobs Dispatched", static_cast<i64>(numJobsDispatched));
    TracyPlot("Model Prepare Requests Coalesced", static_cast<i64>(numRequestsCoalesced));
    TracyPlot("Model Prepare Requests Deferred", static_cast<i64>(numRequestsDeferred));
}

void ModelLoader::CancelAndDrainPrepareJobs()
{
    ZoneScopedN("ModelLoader::CancelAndDrainPrepareJobs");
    if (!_activeModelPrepareJobs.empty())
    {
        enki::TaskScheduler* taskScheduler = ServiceLocator::GetTaskScheduler();
        for (const std::unique_ptr<ActiveModelPrepareJob>& job : _activeModelPrepareJobs)
            taskScheduler->WaitforTask(job.get());

        _activeModelPrepareJobs.clear();
    }

    ModelLoading::PreparedModelResult result;
    while (_preparedModelResults.try_dequeue(result))
    {
        // Discard results invalidated by Clear or destruction.
    }

    _pendingPreparedModelCommits.clear();
}

void ModelLoader::CompletePreparedRequest(const LoadRequestInternal& request, bool success)
{
    const bool isStatic = request.type == LoadRequestType::Placement || request.type == LoadRequestType::Decoration;
    if (isStatic)
        _numTerrainModelsLoaded++;

    if (success)
        _internalLoadRequests.enqueue(request);
    else
        EnqueueLoadResult(request, false, isStatic);
}

bool ModelLoader::IsCurrentEntityRequest(const LoadRequestInternal& request) const
{
    if (request.type != LoadRequestType::Model && request.type != LoadRequestType::DisplayID)
        return true;

    if (request.entity == entt::null)
        return true;

    const u32 entity = entt::to_integral(request.entity);
    auto requestItr = _entityToLatestRequestID.find(entity);
    return requestItr != _entityToLatestRequestID.end() && requestItr->second == request.requestID;
}

ModelLoading::ModelLoadRequestID ModelLoader::GetNextLoadRequestID()
{
    return _nextLoadRequestID.fetch_add(1, std::memory_order_relaxed);
}

void ModelLoader::EnqueueLoadResult(const LoadRequestInternal& request, bool success, bool isStatic)
{
    NC_ASSERT(request.requestID != ModelLoading::INVALID_MODEL_LOAD_REQUEST_ID, "ModelLoader cannot complete a request without a stable request ID");

    _loadRequestResults.enqueue({
        .requestID = request.requestID,
        .request = request,
        .success = success,
        .isStatic = isStatic,
    });
}

bool ModelLoader::LoadRequest(DiscoveredModel& discoveredModel)
{
    ZoneScopedN("ModelLoader::LoadRequest");

    if (!discoveredModel.model)
    {
        return false;
    }

    if (discoveredModel.model->modelHeader.numVertices == 0)
    {
        NC_LOG_ERROR("ModelLoader : Tried to load model ({0}) without any vertices", discoveredModel.name);
        return false;
    }

    ModelLoading::ModelBuildResult buildResult = ModelLoading::BuildPreparedModel(discoveredModel.name, *discoveredModel.model);
    if (!buildResult)
    {
        NC_LOG_ERROR("ModelLoader : Failed to prepare model ({0}): {1}", discoveredModel.name, buildResult.error);
        return false;
    }

    return CommitPreparedModel(discoveredModel, buildResult.preparedModel);
}

bool ModelLoader::CommitPreparedModel(DiscoveredModel& discoveredModel, const ModelLoading::PreparedRenderModel& preparedModel)
{
    ZoneScopedN("ModelLoader::CommitPreparedModel");

    u32 modelID = _modelRenderer->CommitPreparedModel(preparedModel);
    _modelHashToModelID[discoveredModel.modelHash] = modelID;
    _modelIDToModelHash[modelID] = discoveredModel.modelHash;

    ECS::Components::AABB& aabb = _modelIDToAABB[modelID];
    aabb.centerPos = discoveredModel.model->aabbCenter;
    aabb.extents = discoveredModel.model->aabbExtents;

    // Generate Jolt Shape
    {
        i32 physicsEnabled = *CVarSystem::Get()->GetIntCVar(CVarCategory::Client | CVarCategory::Physics, "enabled"_h);
        u32 numPhysicsBytes = static_cast<u32>(discoveredModel.model->physicsData.size());

        if (physicsEnabled && numPhysicsBytes > 0)
        {
            ZoneScopedN("Load Physics Shape");

            Bytebuffer physicsBuffer = Bytebuffer(discoveredModel.model->physicsData.data(), numPhysicsBytes);
            physicsBuffer.SkipWrite(numPhysicsBytes);

            JoltStreamIn streamIn(&physicsBuffer);

            JPH::Shape::IDToShapeMap shapeMap;
            JPH::Shape::IDToMaterialMap materialMap;

            JPH::MeshShapeSettings::ShapeResult shapeResult = JPH::Shape::sRestoreWithChildren(streamIn, shapeMap, materialMap);
            discoveredModel.hasShape = true;

            {
                std::scoped_lock lock(_physicsSystemMutex);
                _modelHashToJoltShape[discoveredModel.modelHash] = shapeResult.Get();
            }
        }
    }

    return true;
}

void ModelLoader::AddStaticInstance(entt::entity entityID, const LoadRequestInternal& request)
{
    ZoneScopedN("ModelLoader::AddStaticInstance");

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
    auto& tSystem = ECS::TransformSystem::Get(*registry);

    auto& name = registry->get<ECS::Components::Name>(entityID);
    DiscoveredModel& discoveredModel = _modelHashToDiscoveredModel[request.modelHash];
    name.name = StringUtils::GetFileNameFromPath(discoveredModel.name);
    name.fullName = discoveredModel.name;
    name.nameHash = discoveredModel.modelHash;

    u32 modelID = _modelHashToModelID[request.modelHash];
    u32 doodadSet = request.type == LoadRequestType::Placement ? static_cast<u32>(request.extraData2) : std::numeric_limits<u32>().max();
    u32 instanceID;
    {
        ZoneScopedN("Commit Static Renderer Instance");
        instanceID = _modelRenderer->AddPlacementInstance(entityID, modelID, request.modelHash, nullptr, request.spawnPosition, request.spawnRotation, request.scale, doodadSet, request.type == LoadRequestType::Placement);
    }

    auto& model = registry->get<ECS::Components::Model>(entityID);
    model.flags.loaded = true;
    model.modelID = modelID;
    model.instanceID = instanceID;
    model.modelHash = request.modelHash;

    const ECS::Components::AABB& modelAABB = _modelIDToAABB[modelID];

    auto& aabb = registry->get<ECS::Components::AABB>(entityID);
    aabb.centerPos = modelAABB.centerPos;
    aabb.extents = modelAABB.extents;

    bool hasParent = false;
    u32 parentInstanceID = static_cast<u32>(request.extraData1);
    {
        std::scoped_lock lock(_instanceIDToModelIDMutex);
        _uniqueIDToinstanceID[static_cast<u32>(request.extraData3)] = instanceID;
        _instanceIDToModelID[instanceID] = modelID;
        _instanceIDToEntityID[instanceID] = entityID;

        hasParent = parentInstanceID != std::numeric_limits<u32>().max() && _instanceIDToEntityID.contains(parentInstanceID);
    }

    {
        std::scoped_lock lock(_transformSystemMutex);

        tSystem.SetLocalTransform(entityID, request.spawnPosition, request.spawnRotation, vec3(request.scale));

        if (hasParent)
        {
            entt::entity parentEntityID = _instanceIDToEntityID[parentInstanceID];
            tSystem.ParentEntityTo(parentEntityID, entityID);
        }
    }

    if (discoveredModel.hasShape)
    {
        i32 physicsEnabled = *CVarSystem::Get()->GetIntCVar(CVarCategory::Client | CVarCategory::Physics, "enabled"_h);

        if (physicsEnabled)
        {
            ZoneScopedN("Add Physics Shape");

            entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
            auto& joltState = registry->ctx().get<ECS::Singletons::JoltState>();
            JPH::BodyInterface& bodyInterface = joltState.physicsSystem.GetBodyInterface();

            const JPH::ShapeRefC& shape = _modelHashToJoltShape[request.modelHash];

            // TODO: We need to scale the shape

            auto& transform = registry->get<ECS::Components::Transform>(entityID);
            vec3 position = transform.GetWorldPosition();
            const quat& rotation = transform.GetWorldRotation();

            // Create the settings for the body itself. Note that here you can also set other properties like the restitution / friction.
            JPH::BodyCreationSettings bodySettings(new JPH::ScaledShapeSettings(shape, JPH::Vec3::sReplicate(request.scale)), JPH::RVec3(position.x, position.y, position.z), JPH::Quat(rotation.x, rotation.y, rotation.z, rotation.w), JPH::EMotionType::Static, Jolt::Layers::NON_MOVING);

            // Create the actual rigid body
            JPH::Body* body = bodyInterface.CreateBody(bodySettings); // Note that if we run out of bodies this can return nullptr
            joltState.RecordBodyCreate(ECS::Singletons::JoltBodyTelemetrySource::StaticPlacement, body != nullptr);
            if (body)
            {
                JPH::BodyID bodyID = body->GetID();

                // Store the entity ID in the body so we can look it up later
                body->SetUserData(static_cast<JPH::uint64>(entityID));
                bodyInterface.AddBody(bodyID, JPH::EActivation::Activate);

                {
                    std::scoped_lock lock(_physicsSystemMutex);
                    _instanceIDToBodyID[instanceID] = bodyID.GetIndexAndSequenceNumber();
                }
            }
        }
    }

    auto& animationInitData = registry->get<ECS::Components::AnimationInitData>(entityID);
    animationInitData.flags.isDynamic = false;
}

void ModelLoader::AddDynamicInstance(entt::entity entityID, const LoadRequestInternal& request)
{
    ZoneScopedN("ModelLoader::AddDynamicInstance");

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;

    const DiscoveredModel& discoveredModel = _modelHashToDiscoveredModel[request.modelHash];
    auto& name = registry->get<ECS::Components::Name>(entityID);
    name.name = StringUtils::GetFileNameFromPath(discoveredModel.name);
    name.fullName = discoveredModel.name;
    name.nameHash = discoveredModel.modelHash;

    auto& model = registry->get<ECS::Components::Model>(entityID);
    model.scale = request.scale;

    u32 modelID = _modelHashToModelID[request.modelHash];
    u32 instanceID = model.instanceID;

    ECS::TransformSystem& transformSystem = ECS::TransformSystem::Get(*registry);
    auto& transform = registry->get<ECS::Components::Transform>(entityID);
    transformSystem.SetLocalScale(entityID, vec3(model.scale));

    if (instanceID == std::numeric_limits<u32>().max())
    {
        ZoneScopedN("Commit New Dynamic Renderer Instance");
        instanceID = _modelRenderer->AddInstance(entityID, modelID, discoveredModel.model, transform.GetMatrix(), request.extraData1);
    }
    else
    {
        ZoneScopedN("Commit Modified Dynamic Renderer Instance");
        _modelRenderer->ModifyInstance(entityID, instanceID, modelID, discoveredModel.model, transform.GetMatrix(), request.extraData1);
    }

    model.modelID = modelID;
    model.instanceID = instanceID;

    const ECS::Components::AABB& modelAABB = _modelIDToAABB[modelID];
    auto& aabb = registry->get<ECS::Components::AABB>(entityID);
    aabb.centerPos = modelAABB.centerPos;
    aabb.extents = modelAABB.extents;

    transform.SetDirty(transformSystem, entityID);

    {
        std::scoped_lock lock(_instanceIDToModelIDMutex);
        _instanceIDToModelID[instanceID] = modelID;
        _instanceIDToEntityID[instanceID] = entityID;
    }

    if (discoveredModel.hasShape)
    {
        i32 physicsEnabled = *CVarSystem::Get()->GetIntCVar(CVarCategory::Client | CVarCategory::Physics, "enabled"_h);

        if (physicsEnabled && registry->all_of<ECS::Components::Unit>(entityID))
        {
            ZoneScopedN("Add Physics Shape");

            entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
            auto& joltState = registry->ctx().get<ECS::Singletons::JoltState>();
            JPH::BodyInterface& bodyInterface = joltState.physicsSystem.GetBodyInterface();
            const JPH::BodyLockInterfaceNoLock& bodyLockInterface = joltState.physicsSystem.GetBodyLockInterfaceNoLock();

            auto& unit = registry->get<ECS::Components::Unit>(entityID);
            if (unit.bodyID != std::numeric_limits<u32>().max())
            {
                JPH::BodyID oldBodyID = static_cast<JPH::BodyID>(unit.bodyID);
                if (JPH::Body* oldBody = bodyLockInterface.TryGetBody(oldBodyID))
                {
                    bodyInterface.RemoveBody(oldBodyID);
                    bodyInterface.DestroyBody(oldBodyID);
                }

                unit.bodyID = std::numeric_limits<u32>().max();
                {
                    std::scoped_lock lock(_physicsSystemMutex);
                    if (_instanceIDToBodyID.contains(instanceID))
                        _instanceIDToBodyID.erase(instanceID);
                }
            }

            const JPH::ShapeRefC& shape = _modelHashToJoltShape[request.modelHash];

            // TODO: We need to scale the shape
            vec3 position = transform.GetWorldPosition();
            const quat& rotation = transform.GetWorldRotation();

            // Create the settings for the body itself. Note that here you can also set other properties like the restitution / friction.
            JPH::BodyCreationSettings bodySettings(new JPH::ScaledShapeSettings(shape, JPH::Vec3(2.0f, 1.0f, 0.5f)), JPH::RVec3(position.x, position.y, position.z), JPH::Quat(rotation.x, rotation.y, rotation.z, rotation.w), JPH::EMotionType::Kinematic, Jolt::Layers::MOVING);
            bodySettings.mAllowedDOFs = JPH::EAllowedDOFs::TranslationX | JPH::EAllowedDOFs::TranslationY | JPH::EAllowedDOFs::TranslationZ;
            bodySettings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
            bodySettings.mMassPropertiesOverride.mMass = 1000.0f;
            bodySettings.mIsSensor = true;

            // Create the actual rigid body
            JPH::Body* body = bodyInterface.CreateBody(bodySettings); // Note that if we run out of bodies this can return nullptr
            joltState.RecordBodyCreate(ECS::Singletons::JoltBodyTelemetrySource::DynamicInstance, body != nullptr);
            if (body)
            {
                JPH::BodyID bodyID = body->GetID();
                unit.bodyID = bodyID.GetIndexAndSequenceNumber();

                // Store the entity ID in the body so we can look it up later
                body->SetUserData(static_cast<JPH::uint64>(entityID));
                bodyInterface.AddBody(bodyID, JPH::EActivation::Activate);

                {
                    std::scoped_lock lock(_physicsSystemMutex);
                    _instanceIDToBodyID[instanceID] = bodyID.GetIndexAndSequenceNumber();
                }
            }
        }
    }

    {
        std::scoped_lock lock(_animationMutex);

        auto& animationInitData = registry->get_or_emplace<ECS::Components::AnimationInitData>(entityID);
        animationInitData.flags.isDynamic = true;
    }

    if (_modelRenderer)
    {
        ZoneScopedN("Initialize Dynamic Model Animation");
        _modelRenderer->AddAnimationInstance(instanceID);
    }
}
