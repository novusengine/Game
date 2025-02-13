#include "ModelLoader.h"
#include "ModelRenderer.h"
#include "Game-Lib/Application/EnttRegistries.h"
#include "Game-Lib/ECS/Singletons/ClientDBCollection.h"
#include "Game-Lib/ECS/Singletons/JoltState.h"
#include "Game-Lib/ECS/Components/AnimationData.h"
#include "Game-Lib/ECS/Components/Events.h"
#include "Game-Lib/ECS/Components/Model.h"
#include "Game-Lib/ECS/Components/Name.h"
#include "Game-Lib/ECS/Components/NetworkedEntity.h"
#include "Game-Lib/ECS/Singletons/Skybox.h"
#include "Game-Lib/ECS/Util/EventUtil.h"
#include "Game-Lib/ECS/Util/Transforms.h"
#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Rendering/Debug/DebugRenderer.h"
#include "Game-Lib/Util/JoltStream.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Base/CVarSystem/CVarSystem.h>
#include <Base/Memory/FileReader.h>
#include <Base/Util/StringUtils.h>

#include <FileFormat/Novus/Map/Map.h>
#include <FileFormat/Novus/Map/MapChunk.h>

#include <entt/entt.hpp>

#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/ScaledShape.h>

#include <atomic>
#include <execution>
#include <filesystem>
#include <mutex>
#include <vector>

namespace fs = std::filesystem;

static const fs::path dataPath = fs::path("Data/");
static const fs::path complexModelPath = dataPath / "ComplexModel";

ModelLoader::ModelLoader(ModelRenderer* modelRenderer)
    : _modelRenderer(modelRenderer)
    , _pendingLoadRequests(MAX_PENDING_LOADS_PER_FRAME)
    , _internalLoadRequests(MAX_INTERNAL_LOADS_PER_FRAME)
    , _discoveredModels()
{
    _pendingLoadRequestsVector.resize(MAX_PENDING_LOADS_PER_FRAME);
    _internalLoadRequestsVector.resize(MAX_INTERNAL_LOADS_PER_FRAME);
}

void ModelLoader::Init()
{
    NC_LOG_INFO("ModelLoader : Scanning for models");

    static const fs::path fileExtension = ".complexmodel";

    if (!fs::exists(complexModelPath))
    {
        fs::create_directories(complexModelPath);
    }

    enki::TaskScheduler* taskScheduler = ServiceLocator::GetTaskScheduler();
    IOLoader* ioLoader = ServiceLocator::GetIOLoader();

    // Then create a multithreaded job to loop over the paths
    _numDiscoveredModelsToLoad = 0;

    fs::path absolutePath = std::filesystem::absolute(complexModelPath).make_preferred();
    std::string absolutePathStr = absolutePath.string();
    size_t subStrIndex = absolutePathStr.length() + 1; // + 1 here for folder seperator

    // First recursively iterate the directory and find all paths
    std::vector<fs::path> paths;
    std::filesystem::recursive_directory_iterator dirpos { absolutePath };
    std::copy(begin(dirpos), end(dirpos), std::back_inserter(paths));

    u32 numPaths = static_cast<u32>(paths.size());
    NC_LOG_INFO("ModelLoader : Processing {0} scanned model paths", numPaths);

    enki::TaskSet discoverModelsTask(numPaths, [&, this, paths, subStrIndex](enki::TaskSetPartition range, u32 threadNum)
    {
        u32 numDiscoveredModelsToLoad = 0;

        IOLoadRequest loadRequest;
        loadRequest.callback = std::bind(&ModelLoader::HandleDiscoverModelCallback, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4);

        for (u32 i = range.start; i < range.end; i++)
        {
            const fs::path& path = paths[i];
            
            if (!path.has_extension() || path.extension().compare(fileExtension) != 0)
                continue;

            std::string pathStr = path.string();
            std::replace(pathStr.begin(), pathStr.end(), '\\', '/');
            loadRequest.path = std::move(pathStr);

            std::string modelPath = loadRequest.path.substr(subStrIndex);
            loadRequest.userdata = StringUtils::fnv1a_32(modelPath.c_str(), modelPath.length());

            ioLoader->RequestLoad(loadRequest);
            numDiscoveredModelsToLoad++;
        }

        _numDiscoveredModelsToLoad += numDiscoveredModelsToLoad;
    });

    // Execute the multithreaded job
    taskScheduler->AddTaskSetToPipe(&discoverModelsTask);
    taskScheduler->WaitforTask(&discoverModelsTask);
}

void ModelLoader::Clear()
{
    LoadRequestInternal dummyRequest;
    while (_pendingLoadRequests.try_dequeue(dummyRequest))
    {
        // Just empty the queue
    }

    while (_internalLoadRequests.try_dequeue(dummyRequest))
    {
        // Just empty the queue
    }

    UnloadRequest dummyUnloadRequest;
    while (_unloadRequests.try_dequeue(dummyUnloadRequest))
    {
        // Just empty the queue
    }

    for (auto& pair : _modelHashToLoadState)
    {
        if (pair.second != LoadState::Failed)
            pair.second = LoadState::NotLoaded;
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
    _modelRenderer->Clear();

    auto& tSystem = ECS::TransformSystem::Get(*registry);
    tSystem.ProcessMovedEntities([](entt::entity entity) { });

    registry->destroy(_createdEntities.begin(), _createdEntities.end());
    _createdEntities.clear();
}

void ModelLoader::Update(f32 deltaTime)
{
    ZoneScopedN("ModelLoader::Update");
    
    enki::TaskScheduler* taskScheduler = ServiceLocator::GetTaskScheduler();
    u32 numPendingWorkRequests = static_cast<u32>(_discoveredModelPendingWorkRequests.size_approx());

    if (numPendingWorkRequests > 0)
    {
        ZoneScopedN("Discover Models");
        u32 maxModelDiscoveryThisTick = glm::min(numPendingWorkRequests, 50u);

        std::atomic<u32> numModelsDiscoveredThisTick = 0;
        enki::TaskSet loadDiscoveredModelTask(maxModelDiscoveryThisTick, [&](enki::TaskSetPartition range, uint32_t threadNum)
        {
            ZoneScopedN("Discover Task");
            u32 numRequestsCompleted = 0;

            WorkRequest workRequest;
            for (u32 i = range.start; i < range.end; i++)
            //for (u32 i = 0; i < maxModelDiscoveryThisTick; i++)
            {
                if (!_discoveredModelPendingWorkRequests.try_dequeue(workRequest))
                    break;

                ZoneScopedN("Discover Work");
                numRequestsCompleted++;

                size_t fileSize = workRequest.data->writtenData;
                constexpr u32 HEADER_SIZE = sizeof(FileHeader) + sizeof(Model::ComplexModel::ModelHeader);
                
                if (fileSize < HEADER_SIZE)
                {
                    NC_LOG_ERROR("ModelLoader : Tried to open model file ({0}) but it was smaller than sizeof(FileHeader) + sizeof(ModelHeader)", workRequest.path);

                    std::scoped_lock lock(_modelHashMutex);
                    _modelHashToLoadState[workRequest.modelHash] = LoadState::Failed;

                    continue;
                }
                
                // Extract the Model from the file and store it as a DiscoveredModel
                DiscoveredModel discoveredModel =
                {
                    .name = std::move(workRequest.path),
                    .modelHash = workRequest.modelHash,
                    .hasShape = false,
                    .model = nullptr
                };

                {
                    ZoneScopedN("Read Model");

                    Model::ComplexModel* model = new Model::ComplexModel();
                    if (!Model::ComplexModel::Read(workRequest.data, *model))
                    {
                        NC_LOG_ERROR("ModelLoader : Failed to read the Model for file ({0})", discoveredModel.name);
                        delete model;

                        std::scoped_lock lock(_modelHashMutex);
                        _modelHashToLoadState[workRequest.modelHash] = LoadState::Failed;

                        continue;
                    }

                    discoveredModel.model = model;
                    _discoveredModels.enqueue(discoveredModel);
                }
            }

            numModelsDiscoveredThisTick += numRequestsCompleted;
        });
        
        taskScheduler->AddTaskSetToPipe(&loadDiscoveredModelTask);
        taskScheduler->WaitforTask(&loadDiscoveredModelTask);

        _numDiscoveredModelsLoaded += numModelsDiscoveredThisTick;
        _modelHashToLoadState.reserve(_modelHashToLoadState.size() + numModelsDiscoveredThisTick);
        _modelHashToLoadingMutex.reserve(_modelHashToLoadingMutex.size() + numModelsDiscoveredThisTick);
        _modelHashToDiscoveredModel.reserve(_modelHashToDiscoveredModel.size() + numModelsDiscoveredThisTick);

        {
            ZoneScopedN("Process Discovered Models");

            DiscoveredModel discoveredModel;
            while (_discoveredModels.try_dequeue(discoveredModel))
            {
                if (_modelHashToDiscoveredModel.contains(discoveredModel.modelHash))
                {
                    const DiscoveredModel& existingDiscoveredModel = _modelHashToDiscoveredModel[discoveredModel.modelHash];
                    NC_LOG_ERROR("ModelLoader : Found duplicate model hash ({0}) for Paths (\"{1}\") - (\"{2}\")", discoveredModel.modelHash, existingDiscoveredModel.name, discoveredModel.name);
                }

                _modelHashToLoadState[discoveredModel.modelHash] = LoadState::NotLoaded;
                _modelHashToLoadingMutex[discoveredModel.modelHash] = new std::mutex();
                _modelHashToDiscoveredModel[discoveredModel.modelHash] = std::move(discoveredModel);
            }
        }
    }

    // Ensure All Discovered Models are loaded before we proceed
    if (_numDiscoveredModelsLoaded < _numDiscoveredModelsToLoad)
        return;

    bool discoveredModelsComplete = _discoveredModelsComplete;
    if (!discoveredModelsComplete)
    {
        ECS::Util::EventUtil::PushEvent(ECS::Components::DiscoveredModelsCompleteEvent{});
        _discoveredModelsComplete = true;
    }

    u32 numDequeuedLoadRequests = static_cast<u32>(_pendingLoadRequests.try_dequeue_bulk(_pendingLoadRequestsVector.data(), MAX_PENDING_LOADS_PER_FRAME));
    if (numDequeuedLoadRequests > 0)
    {
        ZoneScopedN("Pending Load Model Requests");
        ModelRenderer::ReserveInfo reserveInfo;

        {
            ZoneScopedN("Calculate Model Reserve Info");

            for (u32 i = 0; i < numDequeuedLoadRequests; i++)
            {
                const LoadRequestInternal& loadRequest = _pendingLoadRequestsVector[i];

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

        {
            ZoneScopedN("Model Reserve");

            _modelHashToModelID.reserve(_modelHashToModelID.size() + reserveInfo.numModels);
            _modelIDToModelHash.reserve(_modelIDToModelHash.size() + reserveInfo.numModels);
            _modelIDToAABB.reserve(_modelIDToAABB.size() + reserveInfo.numModels);
            _modelIDToAABB.reserve(_modelIDToAABB.size() + reserveInfo.numModels);
            _modelHashToLoadingMutex.reserve(_modelHashToLoadingMutex.size() + reserveInfo.numModels);

            _modelRenderer->Reserve(reserveInfo);
        }

        //enki::TaskSet loadModelsTask(numDequeuedLoadRequests, [&](enki::TaskSetPartition range, uint32_t threadNum)
        //{
            //ZoneScopedN("Load Model Task");
            //for (u32 i = range.start; i < range.end; i++)
            for (u32 i = 0; i < numDequeuedLoadRequests; i++)
            {
                ZoneScopedN("Load Model Work");

                const LoadRequestInternal& loadRequest = _pendingLoadRequestsVector[i];
                if (!_modelHashToDiscoveredModel.contains(loadRequest.modelHash))
                    continue;

                std::mutex* modelMutex = _modelHashToLoadingMutex[loadRequest.modelHash];
                std::scoped_lock lock(*modelMutex);

                LoadState loadState = _modelHashToLoadState[loadRequest.modelHash];
                if (loadState == LoadState::NotLoaded)
                {
                    ZoneScopedN("Load Model Request");
                    DiscoveredModel& discoveredModel = _modelHashToDiscoveredModel[loadRequest.modelHash];

                    bool didLoad = LoadRequest(discoveredModel);
                    loadState = static_cast<LoadState>((LoadState::Loaded * didLoad) + (LoadState::Failed * !didLoad));
                    _modelHashToLoadState[loadRequest.modelHash] = loadState;
                }

                if (loadState == LoadState::Failed)
                    continue;

                _internalLoadRequests.enqueue(loadRequest);
            }
        //});

        //taskScheduler->AddTaskSetToPipe(&loadModelsTask);
        //taskScheduler->WaitforTask(&loadModelsTask);
    }

    u32 numDequeuedInternalRequests = static_cast<u32>(_internalLoadRequests.try_dequeue_bulk(_internalLoadRequestsVector.data(), MAX_INTERNAL_LOADS_PER_FRAME));
    if (numDequeuedInternalRequests > 0)
    {
        ZoneScopedN("Pending Load Instance Requests");

        ModelRenderer::ReserveInfo reserveInfo;

        {
            ZoneScopedN("Calculate Instance Reserve Info");

            for (u32 i = 0; i < numDequeuedInternalRequests; i++)
            {
                const LoadRequestInternal& loadRequest = _internalLoadRequestsVector[i];

                u32 modelHash = loadRequest.modelHash;
                const DiscoveredModel& discoveredModel = _modelHashToDiscoveredModel[modelHash];
                bool isSupported = discoveredModel.model->modelHeader.numVertices > 0;
                bool hasDisplayID = loadRequest.type == LoadRequestType::DisplayID;

                // Only increment Instance Count & Drawcall Count if the model have vertices
                {
                    reserveInfo.numInstances += 1 * isSupported;
                    //reserveInfo.numOpaqueDrawcalls += discoveredModel.model->modelHeader.numOpaqueRenderBatches * isSupported;
                    //reserveInfo.numTransparentDrawcalls += discoveredModel.model->modelHeader.numTransparentRenderBatches * isSupported;
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

                _modelHashToJoltShape.reserve(_modelHashToJoltShape.size() + reserveInfo.numInstances);
                _uniqueIDToinstanceID.reserve(_uniqueIDToinstanceID.size() + reserveInfo.numInstances);
                _instanceIDToModelID.reserve(_instanceIDToModelID.size() + reserveInfo.numInstances);
                _instanceIDToBodyID.reserve(_instanceIDToBodyID.size() + reserveInfo.numInstances);
                _instanceIDToEntityID.reserve(_instanceIDToEntityID.size() + reserveInfo.numInstances);

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
                for (u32 i = 0; i < numDequeuedInternalRequests; i++)
                {
                    ZoneScopedN("Load Instance Work");
                    const LoadRequestInternal& loadRequest = _internalLoadRequestsVector[i];

                    const DiscoveredModel& discoveredModel = _modelHashToDiscoveredModel[loadRequest.modelHash];
                    bool isSupported = discoveredModel.model->modelHeader.numVertices > 0;
                    if (!isSupported)
                        continue;

                    switch (loadRequest.type)
                    {
                        case LoadRequestType::Placement:
                        case LoadRequestType::Decoration:
                        {
                            ZoneScopedN("Load Placement / Decoration");

                            bool hasUniqueID = loadRequest.uniqueID != std::numeric_limits<u32>().max();
                            bool uniqueIDExists = _uniqueIDToinstanceID.contains(loadRequest.uniqueID);

                            if (hasUniqueID && uniqueIDExists)
                                break;

                            if (loadRequest.entity == entt::null)
                            {
                                u32 index = static_cast<u32>(createdEntitiesOffset) + numCreatedInstances.fetch_add(1);
                                AddStaticInstance(_createdEntities[index], loadRequest);
                            }
                            else
                            {
                                AddStaticInstance(loadRequest.entity, loadRequest);
                            }

                            break;
                        }

                        case LoadRequestType::Model:
                        case LoadRequestType::DisplayID:
                        {
                            ZoneScopedN("Load Model / DisplayID");
                            if (loadRequest.entity == entt::null)
                            {
                                u32 index = static_cast<u32>(createdEntitiesOffset) + numCreatedInstances.fetch_add(1);
                                AddDynamicInstance(_createdEntities[index], loadRequest);
                            }
                            else
                            {
                                AddDynamicInstance(loadRequest.entity, loadRequest);
                            }

                            break;
                        }

                        default: break;
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
    
    size_t unloadRequests = _unloadRequests.size_approx();
    if (unloadRequests)
    {
        ZoneScopedN("Unload Requests Task");

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        mat4x4 identity = mat4x4(1.0f);

        UnloadRequest unloadRequest;
        while (_unloadRequests.try_dequeue(unloadRequest))
        {
            _modelRenderer->RemoveInstance(unloadRequest.instanceID);
            //_modelRenderer->ModifyInstance(unloadRequest.entity, unloadRequest.instanceID, std::numeric_limits<u32>().max(), nullptr, identity);
        }
    }
}

entt::entity ModelLoader::CreateModelEntity(const std::string& name)
{
    entt::registry& registry = *ServiceLocator::GetEnttRegistries()->gameRegistry;

    entt::entity entity = registry.create();
    auto& nameComponent = registry.emplace<ECS::Components::Name>(entity);
    nameComponent.name = name;
    nameComponent.nameHash = StringUtils::fnv1a_32(name.c_str(), name.size());
    nameComponent.fullName = "";

    registry.emplace<ECS::Components::AABB>(entity);
    registry.emplace<ECS::Components::Transform>(entity);
    registry.emplace<ECS::Components::Model>(entity);

    return entt::entity(entity);
}

void ModelLoader::LoadPlacement(const Terrain::Placement& placement)
{
    LoadRequestInternal loadRequest;

    loadRequest.type = LoadRequestType::Placement;
    loadRequest.entity = entt::null;
    loadRequest.uniqueID = placement.uniqueID;
    loadRequest.modelHash = placement.nameHash;

    // Terrain::Placement uses u16::max for doodadSet when the field is unused, but we need to use u32::max for extraData when it is unused
    bool isDoodadSetPresent = placement.doodadSet != std::numeric_limits<u16>().max();
    constexpr u32 defaultValue = std::numeric_limits<u32>().max();
    u32 doodadSet = (placement.doodadSet * isDoodadSetPresent) + (defaultValue * !isDoodadSetPresent);
    loadRequest.extraData = doodadSet;

    loadRequest.spawnPosition = placement.position;
    loadRequest.spawnRotation = placement.rotation;
    loadRequest.scale = static_cast<f32>(placement.scale) / 1024.0f;

    _pendingLoadRequests.enqueue(loadRequest);
}

void ModelLoader::LoadDecoration(u32 instanceID, const Model::ComplexModel::Decoration& decoration)
{
    LoadRequestInternal loadRequest;
    loadRequest.type = LoadRequestType::Decoration;
    loadRequest.entity = entt::null;
    loadRequest.instanceID = instanceID;
    loadRequest.modelHash = decoration.nameID;
    loadRequest.extraData = decoration.color;
    loadRequest.spawnPosition = decoration.position;
    loadRequest.spawnRotation = decoration.rotation;
    loadRequest.scale = decoration.scale;

    _pendingLoadRequests.enqueue(loadRequest);
}

bool ModelLoader::LoadModelForEntity(entt::entity entity, ECS::Components::Model& model, u32 modelNameHash)
{
    if (!_modelHashToDiscoveredModel.contains(modelNameHash))
        return false;

    LoadRequestInternal loadRequest;
    loadRequest.type = LoadRequestType::Model;
    loadRequest.entity = entity;
    loadRequest.modelHash = modelNameHash;
    model.modelHash = modelNameHash;

    _pendingLoadRequests.enqueue(loadRequest);

    return true;
}

bool ModelLoader::LoadDisplayIDForEntity(entt::entity entity, ECS::Components::Model& model, ClientDB::Definitions::DisplayInfoType displayInfoType, u32 displayID)
{
    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
    auto& clientDBCollection = registry->ctx().get<ECS::Singletons::ClientDBCollection>();

    u32 modelHash = std::numeric_limits<u32>().max();

    switch (displayInfoType)
    {
        case ClientDB::Definitions::DisplayInfoType::Creature:
        {
            auto* creatureDisplayInfoStorage = clientDBCollection.Get(ECS::Singletons::ClientDBHash::CreatureDisplayInfo);
            auto* creatureModelDataStorage = clientDBCollection.Get(ECS::Singletons::ClientDBHash::CreatureModelData);

            if (!creatureDisplayInfoStorage->Has(displayID))
                return false;

            const ClientDB::Definitions::CreatureDisplayInfo& creatureDisplayInfo = creatureDisplayInfoStorage->Get<ClientDB::Definitions::CreatureDisplayInfo>(displayID);
            const ClientDB::Definitions::CreatureModelData& creatureModelData = creatureModelDataStorage->Get<ClientDB::Definitions::CreatureModelData>(creatureDisplayInfo.modelID);

            modelHash = creatureModelData.modelHash;
            break;
        }
        
        case ClientDB::Definitions::DisplayInfoType::GameObject:
        {
            break;
        }
        
        case ClientDB::Definitions::DisplayInfoType::Item:
        {
            auto* modelFileDataStorage = clientDBCollection.Get(ECS::Singletons::ClientDBHash::ModelFileData);
            auto* itemDisplayInfoStorage = clientDBCollection.Get(ECS::Singletons::ClientDBHash::ItemDisplayInfo);

            if (!itemDisplayInfoStorage->Has(displayID))
                return false;

            const ClientDB::Definitions::ItemDisplayInfo& itemDisplayInfo = itemDisplayInfoStorage->Get<ClientDB::Definitions::ItemDisplayInfo>(displayID);

            u32 itemModelHash = std::numeric_limits<u32>().max();
            u32 modelResourcesID = itemDisplayInfo.modelResourcesID[0];

            // TODO : This is a hack to bypass a lookup table for now. A lookup table is needed so that we can go from modelResourcesID -> List<ModelFileData>
            modelFileDataStorage->Each([&itemModelHash, modelResourcesID](u32 id, const ClientDB::Definitions::ModelFileData& modelFileData) -> bool
            {
                if (modelFileData.modelResourcesID != modelResourcesID)
                    return true;

                itemModelHash = modelFileData.modelHash;
                return false;
            });

            modelHash = itemModelHash;
            break;
        }

        default: break;
    }

    if (!_modelHashToDiscoveredModel.contains(modelHash))
        return false;

    LoadRequestInternal loadRequest;
    loadRequest.type = LoadRequestType::DisplayID;
    loadRequest.entity = entity;

    u32 displayInfoPacked = displayID | static_cast<u32>(displayInfoType) << 24;
    loadRequest.extraData = displayInfoPacked;
    loadRequest.modelHash = modelHash;
    model.modelHash = modelHash;

    _pendingLoadRequests.enqueue(loadRequest);
    return true;
}

void ModelLoader::UnloadModelForEntity(entt::entity entity, ECS::Components::Model& model)
{
    if (model.instanceID == std::numeric_limits<u32>().max())
        return;

    UnloadRequest unloadRequest =
    {
        .entity = entity,
        .instanceID = model.instanceID
    };
    model.modelHash = std::numeric_limits<u32>().max();

    _unloadRequests.enqueue(unloadRequest);
}

void ModelLoader::EnableGroupForEntity(entt::entity entity, u32 groupID)
{
    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
    auto& modelComponent = registry->get<ECS::Components::Model>(entity);

    EnableGroupForModel(modelComponent, groupID);
}

void ModelLoader::EnableGroupForModel(ECS::Components::Model& model, u32 groupID)
{
    if (model.instanceID == std::numeric_limits<u32>().max())
        return;

    _modelRenderer->RequestChangeGroup(model.instanceID, groupID, 0, true);
}

void ModelLoader::DisableGroupForEntity(entt::entity entity, u32 groupID)
{
    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
    auto& modelComponent = registry->get<ECS::Components::Model>(entity);

    DisableGroupForModel(modelComponent, groupID);
}

void ModelLoader::DisableGroupForModel(ECS::Components::Model& model, u32 groupID)
{
    if (model.instanceID == std::numeric_limits<u32>().max())
        return;

    _modelRenderer->RequestChangeGroup(model.instanceID, groupID, 0, false);
}

void ModelLoader::DisableGroupsForEntity(entt::entity entity, u32 groupIDStart, u32 groupIDEnd)
{
    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
    auto& modelComponent = registry->get<ECS::Components::Model>(entity);

    DisableGroupsForModel(modelComponent, groupIDStart, groupIDEnd);
}

void ModelLoader::DisableGroupsForModel(ECS::Components::Model& model, u32 groupIDStart, u32 groupIDEnd)
{
    if (model.instanceID == std::numeric_limits<u32>().max())
        return;

    _modelRenderer->RequestChangeGroup(model.instanceID, groupIDStart, groupIDEnd, false);
}

void ModelLoader::DisableAllGroupsForEntity(entt::entity entity)
{
    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
    auto& modelComponent = registry->get<ECS::Components::Model>(entity);

    DisableAllGroupsForModel(modelComponent);
}

void ModelLoader::DisableAllGroupsForModel(ECS::Components::Model& model)
{
    if (model.instanceID == std::numeric_limits<u32>().max())
        return;

    _modelRenderer->RequestChangeGroup(model.instanceID, 1, std::numeric_limits<u32>().max(), false);
}

const Model::ComplexModel* ModelLoader::GetModelInfo(u32 modelHash)
{
    if (!_modelHashToDiscoveredModel.contains(modelHash))
        return nullptr;

    const auto& discoveredModel = _modelHashToDiscoveredModel[modelHash];
    return discoveredModel.model;
}

u32 ModelLoader::GetModelHashFromModelPath(const std::string& modelPath)
{
    u32 modelHash = StringUtils::fnv1a_32(modelPath.c_str(), modelPath.length());
    return modelHash;
}

bool ModelLoader::GetModelIDFromInstanceID(u32 instanceID, u32& modelID)
{
    if (!_instanceIDToModelID.contains(instanceID))
        return false;

    modelID = _instanceIDToModelID[instanceID];
    return true;
}

bool ModelLoader::GetEntityIDFromInstanceID(u32 instanceID, entt::entity& entityID)
{
    if (!_instanceIDToEntityID.contains(instanceID))
        return false;

    entityID = _instanceIDToEntityID[instanceID];
    return true;
}

bool ModelLoader::ContainsDiscoveredModel(u32 modelNameHash)
{
    return _modelHashToDiscoveredModel.contains(modelNameHash);
}

ModelLoader::DiscoveredModel& ModelLoader::GetDiscoveredModelFromModelID(u32 modelID)
{
    if (!_modelIDToModelHash.contains(modelID))
    {
        NC_LOG_CRITICAL("ModelLoader : Tried to access DiscoveredModel of invalid ModelID {0}", modelID);
    }

    u32 modelHash = _modelIDToModelHash[modelID];
    if (!_modelHashToDiscoveredModel.contains(modelHash))
    {
        NC_LOG_CRITICAL("ModelLoader : Tried to access DiscoveredModel of invalid ModelHash {0}", modelHash);
    }

    return _modelHashToDiscoveredModel[modelHash];
}

bool ModelLoader::LoadRequest(DiscoveredModel& discoveredModel)
{
    if (!discoveredModel.model)
    {
        return false;
    }

    if (discoveredModel.model->modelHeader.numVertices == 0)
    {
        NC_LOG_ERROR("ModelLoader : Tried to load model ({0}) without any vertices", discoveredModel.name);
        return false;
    }

    u32 modelID = _modelRenderer->LoadModel(discoveredModel.name, *discoveredModel.model);
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
            std::shared_ptr<Bytebuffer> physicsBuffer = std::make_shared<Bytebuffer>(discoveredModel.model->physicsData.data(), numPhysicsBytes);
            JoltStreamIn streamIn(physicsBuffer);

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
    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
    auto& tSystem = ECS::TransformSystem::Get(*registry);

    auto& name = registry->get<ECS::Components::Name>(entityID);
    DiscoveredModel& discoveredModel = _modelHashToDiscoveredModel[request.modelHash];
    name.name = StringUtils::GetFileNameFromPath(discoveredModel.name);
    name.fullName = discoveredModel.name;
    name.nameHash = discoveredModel.modelHash;

    u32 modelID = _modelHashToModelID[request.modelHash];
    u32 instanceID = _modelRenderer->AddPlacementInstance(entityID, modelID, nullptr, request.spawnPosition, request.spawnRotation, request.scale, request.extraData);

    auto& model = registry->get<ECS::Components::Model>(entityID);
    model.modelID = modelID;
    model.instanceID = instanceID;
    model.modelHash = request.modelHash;

    const ECS::Components::AABB& modelAABB = _modelIDToAABB[modelID];

    auto& aabb = registry->get<ECS::Components::AABB>(entityID);
    aabb.centerPos = modelAABB.centerPos;
    aabb.extents = modelAABB.extents;

    bool hasParent = false;
    {
        std::scoped_lock lock(_instanceIDToModelIDMutex);
        _uniqueIDToinstanceID[request.uniqueID] = instanceID;
        _instanceIDToModelID[instanceID] = modelID;
        _instanceIDToEntityID[instanceID] = entityID;

        hasParent = request.instanceID != std::numeric_limits<u32>().max() && _instanceIDToEntityID.contains(request.instanceID);
    }

    {
        std::scoped_lock lock(_transformSystemMutex);

        tSystem.SetLocalTransform(entityID, request.spawnPosition, request.spawnRotation, vec3(request.scale));

        if (hasParent)
        {
            entt::entity parentEntityID = _instanceIDToEntityID[request.instanceID];
            tSystem.ParentEntityTo(parentEntityID, entityID);
        }
    }

    if (discoveredModel.hasShape)
    {
        i32 physicsEnabled = *CVarSystem::Get()->GetIntCVar(CVarCategory::Client | CVarCategory::Physics, "enabled"_h);
    
        if (physicsEnabled)
        {
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
    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;

    const DiscoveredModel& discoveredModel = _modelHashToDiscoveredModel[request.modelHash];
    auto& name = registry->get<ECS::Components::Name>(entityID);
    name.name = StringUtils::GetFileNameFromPath(discoveredModel.name);
    name.fullName = discoveredModel.name;
    name.nameHash = discoveredModel.modelHash;

    auto& model = registry->get<ECS::Components::Model>(entityID);

    u32 modelID = _modelHashToModelID[request.modelHash];
    u32 instanceID = model.instanceID;

    auto& transform = registry->get<ECS::Components::Transform>(entityID);
    if (instanceID == std::numeric_limits<u32>().max())
    {
        instanceID = _modelRenderer->AddInstance(entityID, modelID, discoveredModel.model, transform.GetMatrix(), request.extraData);
    }
    else
    {
        _modelRenderer->ModifyInstance(entityID, instanceID, modelID, discoveredModel.model, transform.GetMatrix(), request.extraData);
    }

    model.modelID = modelID;
    model.instanceID = instanceID;

    const ECS::Components::AABB& modelAABB = _modelIDToAABB[modelID];
    auto& aabb = registry->get<ECS::Components::AABB>(entityID);
    aabb.centerPos = modelAABB.centerPos;
    aabb.extents = modelAABB.extents;

    {
        std::scoped_lock lock(_instanceIDToModelIDMutex);
        _instanceIDToModelID[instanceID] = modelID;
        _instanceIDToEntityID[instanceID] = entityID;
    }

    if (discoveredModel.hasShape)
    {
        i32 physicsEnabled = *CVarSystem::Get()->GetIntCVar(CVarCategory::Client | CVarCategory::Physics, "enabled"_h);

        if (physicsEnabled && registry->all_of<ECS::Components::NetworkedEntity>(entityID))
        {
            entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
            auto& joltState = registry->ctx().get<ECS::Singletons::JoltState>();
            JPH::BodyInterface& bodyInterface = joltState.physicsSystem.GetBodyInterface();

            const JPH::ShapeRefC& shape = _modelHashToJoltShape[request.modelHash];

            // TODO: We need to scale the shape
            vec3 position = transform.GetWorldPosition();
            const quat& rotation = transform.GetWorldRotation();

            // Create the settings for the body itself. Note that here you can also set other properties like the restitution / friction.
            JPH::BodyCreationSettings bodySettings(new JPH::ScaledShapeSettings(shape, JPH::Vec3(2.0f, 1.0f, 0.5f)), JPH::RVec3(position.x, position.y, position.z), JPH::Quat(rotation.x, rotation.y, rotation.z, rotation.w), JPH::EMotionType::Kinematic, Jolt::Layers::NON_MOVING);
            bodySettings.mAllowedDOFs = JPH::EAllowedDOFs::TranslationX | JPH::EAllowedDOFs::TranslationY | JPH::EAllowedDOFs::TranslationZ;
            bodySettings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
            bodySettings.mMassPropertiesOverride.mMass = 1000.0f;
            bodySettings.mIsSensor = true;

            // Create the actual rigid body
            JPH::Body* body = bodyInterface.CreateBody(bodySettings); // Note that if we run out of bodies this can return nullptr
            if (body)
            {
                JPH::BodyID bodyID = body->GetID();

                auto& networkedEntity = registry->get<ECS::Components::NetworkedEntity>(entityID);
                networkedEntity.bodyID = bodyID.GetIndexAndSequenceNumber();

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
        _modelRenderer->AddAnimationInstance(instanceID);
    }
}

void ModelLoader::HandleDiscoverModelCallback(bool result, std::shared_ptr<Bytebuffer> buffer, const std::string& path, u64 userdata)
{
    if (!result)
    {
        NC_LOG_WARNING("ModelLoader : Failed to Load ({0})", path);
        return;
    }

    WorkRequest workRequest =
    {
        .path = path,
        .modelHash = static_cast<u32>(userdata),
        .data = std::move(buffer)
    };

    _discoveredModelPendingWorkRequests.enqueue(workRequest);
}
