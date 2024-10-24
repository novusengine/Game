#include "ModelLoader.h"
#include "ModelRenderer.h"
#include "Game-Lib/Animation/AnimationSystem.h"
#include "Game-Lib/Application/EnttRegistries.h"
#include "Game-Lib/ECS/Singletons/JoltState.h"
#include "Game-Lib/ECS/Components/AnimationData.h"
#include "Game-Lib/ECS/Components/Model.h"
#include "Game-Lib/ECS/Components/Name.h"
#include "Game-Lib/ECS/Components/NetworkedEntity.h"
#include "Game-Lib/ECS/Singletons/Skybox.h"
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
#include <Game-Lib/ECS/Singletons/ClientDBCollection.h>

namespace fs = std::filesystem;

static const fs::path dataPath = fs::path("Data/");
static const fs::path complexModelPath = dataPath / "ComplexModel/";

ModelLoader::ModelLoader(ModelRenderer* modelRenderer)
    : _modelRenderer(modelRenderer)
    , _staticRequests(MAX_STATIC_LOADS_PER_FRAME)
    , _dynamicRequests(MAX_DYNAMIC_LOADS_PER_FRAME) { }

void ModelLoader::Init()
{
    NC_LOG_INFO("ModelLoader : Scanning for models");

    static const fs::path fileExtension = ".complexmodel";

    if (!fs::exists(complexModelPath))
    {
        fs::create_directories(complexModelPath);
    }

    enki::TaskScheduler* taskScheduler = ServiceLocator::GetTaskScheduler();

    // First recursively iterate the directory and find all paths
    std::vector<fs::path> paths;
    std::filesystem::recursive_directory_iterator dirpos { complexModelPath };
    std::copy(begin(dirpos), end(dirpos), std::back_inserter(paths));

    // Then create a multithreaded job to loop over the paths
    moodycamel::ConcurrentQueue<DiscoveredModel> discoveredModels;
    enki::TaskSet discoverModelsTask(static_cast<u32>(paths.size()), [&, paths](enki::TaskSetPartition range, u32 threadNum)
    {
        for (u32 i = range.start; i < range.end; i++)
        {
            const fs::path& path = paths[i];
            std::string pathStr = path.string();

            if (!path.has_extension() || path.extension().compare(fileExtension) != 0)
                continue;

            fs::path relativePath = fs::relative(path, complexModelPath);

            PRAGMA_MSVC_IGNORE_WARNING(4244);
            std::string cModelPath = relativePath.string();
            std::replace(cModelPath.begin(), cModelPath.end(), L'\\', L'/');

            FileReader cModelFile(path.string());
            if (!cModelFile.Open())
            {
                NC_LOG_CRITICAL("ModelLoader : Failed to open CModel file: {0}", path.string());
                continue;
            }

            // Load the first HEADER_SIZE of the file into memory
            size_t fileSize = cModelFile.Length();
            constexpr u32 HEADER_SIZE = sizeof(FileHeader) + sizeof(Model::ComplexModel::ModelHeader);

            if (fileSize < HEADER_SIZE)
            {
                NC_LOG_ERROR("ModelLoader : Tried to open CModel file ({0}) but it was smaller than sizeof(FileHeader) + sizeof(ModelHeader)", pathStr);
                continue;
            }

            std::shared_ptr<Bytebuffer> cModelBuffer = Bytebuffer::Borrow<HEADER_SIZE>();

            cModelFile.Read(cModelBuffer.get(), HEADER_SIZE);
            cModelFile.Close();

            // Extract the ModelHeader from the file and store it as a DiscoveredModel
            DiscoveredModel discoveredModel;
            if (!Model::ComplexModel::ReadHeader(cModelBuffer, discoveredModel.modelHeader))
            {
                NC_LOG_ERROR("ModelLoader : Failed to read the ModelHeader for CModel file ({0})", pathStr);
                continue;
            }

            discoveredModel.name = cModelPath;
            discoveredModel.nameHash = StringUtils::fnv1a_32(cModelPath.c_str(), cModelPath.length());
            discoveredModel.hasShape = false;

            discoveredModels.enqueue(discoveredModel);
        }
    });

    // Execute the multithreaded job
    taskScheduler->AddTaskSetToPipe(&discoverModelsTask);
    taskScheduler->WaitforTask(&discoverModelsTask);

    // And lastly move the data into the hashmap

    size_t numDiscoveredModels = discoveredModels.size_approx();
    _nameHashToDiscoveredModel.reserve(numDiscoveredModels);

    DiscoveredModel discoveredModel;
    while (discoveredModels.try_dequeue(discoveredModel))
    {
        if (_nameHashToDiscoveredModel.contains(discoveredModel.nameHash))
        {
            const DiscoveredModel& existingDiscoveredModel = _nameHashToDiscoveredModel[discoveredModel.nameHash];
            NC_LOG_ERROR("Found duplicate model hash ({0}) for Paths (\"{1}\") - (\"{2}\")", discoveredModel.nameHash, existingDiscoveredModel.name, discoveredModel.name);
        }

        _nameHashToDiscoveredModel[discoveredModel.nameHash] = discoveredModel;
    }

    NC_LOG_INFO("Found {0} models", _nameHashToDiscoveredModel.size());

    _staticLoadRequests.resize(MAX_STATIC_LOADS_PER_FRAME);
    _dynamicLoadRequests.resize(MAX_DYNAMIC_LOADS_PER_FRAME);
}

void ModelLoader::Clear()
{
    LoadRequestInternal dummyRequest;
    while (_staticRequests.try_dequeue(dummyRequest))
    {
        // Just empty the queue
    }
    while (_dynamicRequests.try_dequeue(dummyRequest))
    {
        // Just empty the queue
    }

    _nameHashToLoadState.clear();
    _nameHashToModelID.clear();

    for (auto& it : _nameHashToLoadingMutex)
    {
        if (it.second != nullptr)
        {
            delete it.second;
            it.second = nullptr;
        }
    }
    _nameHashToLoadingMutex.clear();

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;

    u32 numInstanceIDToBodyIDs = static_cast<u32>(_instanceIDToBodyID.size());
    if (numInstanceIDToBodyIDs > 0)
    {
        auto& joltState = registry->ctx().get<ECS::Singletons::JoltState>();
        JPH::BodyInterface& bodyInterface = joltState.physicsSystem.GetBodyInterface();

        for (auto& pair : _instanceIDToBodyID)
        {
            JPH::BodyID id = static_cast<JPH::BodyID>(pair.second);

            bodyInterface.RemoveBody(id);
            bodyInterface.DestroyBody(id);
        }

        _instanceIDToBodyID.clear();
    }

    for (auto& pair : _nameHashToJoltShape)
    {
        pair.second = nullptr;
    }
    _nameHashToJoltShape.clear();

    _uniqueIDToinstanceID.clear();
    _instanceIDToModelID.clear();
    _instanceIDToEntityID.clear();
    _modelIDToNameHash.clear();

    for (auto& it : _modelIDToComplexModel)
    {
        if (it.second != nullptr)
        {
            delete it.second;
            it.second = nullptr;
        }
    }
    _modelIDToComplexModel.clear();

    _modelRenderer->Clear();

    ServiceLocator::GetAnimationSystem()->Clear(*registry);

    registry->destroy(_createdEntities.begin(), _createdEntities.end());
    
    _createdEntities.clear();
}

void ModelLoader::Update(f32 deltaTime)
{
    Animation::AnimationSystem* animationSystem = ServiceLocator::GetAnimationSystem();
    enki::TaskScheduler* taskScheduler = ServiceLocator::GetTaskScheduler();

    size_t staticRequests = _staticRequests.size_approx();
    if (staticRequests > 0)
    {
        u32 numDequeued = static_cast<u32>(_staticRequests.try_dequeue_bulk(&_staticLoadRequests[0], MAX_STATIC_LOADS_PER_FRAME));
        if (numDequeued > 0)
        {
            ModelRenderer::ReserveInfo reserveInfo;

            for (u32 i = 0; i < numDequeued; i++)
            {
                LoadRequestInternal& request = _staticLoadRequests[i];
                u32 nameHash = request.placement.nameHash;

                if (!_nameHashToDiscoveredModel.contains(nameHash))
                {
                    NC_LOG_ERROR("ModelLoader : Tried to load model with hash ({0}) which wasn't discovered", nameHash);
                    continue;
                }

                DiscoveredModel& discoveredModel = _nameHashToDiscoveredModel[nameHash];
                bool isSupported = discoveredModel.modelHeader.numVertices > 0;

                // Only increment Instance Count & Drawcall Count if the model have vertices
                {
                    reserveInfo.numInstances += 1 * isSupported;
                    reserveInfo.numOpaqueDrawcalls += discoveredModel.modelHeader.numOpaqueRenderBatches * isSupported;
                    reserveInfo.numTransparentDrawcalls += discoveredModel.modelHeader.numTransparentRenderBatches * isSupported;
                    reserveInfo.numBones += discoveredModel.modelHeader.numBones * isSupported;
                    reserveInfo.numTextureTransforms += discoveredModel.modelHeader.numTextureTransforms * isSupported;
                }

                if (!_nameHashToLoadState.contains(nameHash))
                {
                    _nameHashToLoadState[nameHash] = static_cast<LoadState>((LoadState::Received * isSupported) + (LoadState::Failed * !isSupported));
                    _nameHashToModelID[nameHash] = 0; // 0 should be a cube representing currently loading or something
                    _nameHashToLoadingMutex[nameHash] = new std::mutex();

                    reserveInfo.numModels += 1 * isSupported;
                    reserveInfo.numVertices += discoveredModel.modelHeader.numVertices * isSupported;
                    reserveInfo.numIndices += discoveredModel.modelHeader.numIndices * isSupported;
                    reserveInfo.numTextureUnits += discoveredModel.modelHeader.numTextureUnits * isSupported;
                    reserveInfo.numDecorationSets += discoveredModel.modelHeader.numDecorationSets * isSupported;
                    reserveInfo.numDecorations += discoveredModel.modelHeader.numDecorations * isSupported;

                    reserveInfo.numUniqueOpaqueDrawcalls += discoveredModel.modelHeader.numOpaqueRenderBatches * isSupported;
                    reserveInfo.numUniqueTransparentDrawcalls += discoveredModel.modelHeader.numTransparentRenderBatches * isSupported;
                }
            }

            // Prepare lookup tables
            _nameHashToModelID.reserve(_nameHashToModelID.size() + reserveInfo.numModels);
            _modelIDToComplexModel.reserve(_modelIDToComplexModel.size() + reserveInfo.numModels);
            _modelIDToNameHash.reserve(_modelIDToNameHash.size() + reserveInfo.numModels);
            _modelIDToAABB.reserve(_modelIDToAABB.size() + reserveInfo.numModels);
            _modelIDToAABB.reserve(_modelIDToAABB.size() + reserveInfo.numModels);
            _nameHashToLoadingMutex.reserve(_nameHashToLoadingMutex.size() + reserveInfo.numModels);

            _nameHashToJoltShape.reserve(_nameHashToJoltShape.size() + reserveInfo.numInstances);
            _uniqueIDToinstanceID.reserve(_uniqueIDToinstanceID.size() + reserveInfo.numInstances);
            _instanceIDToModelID.reserve(_instanceIDToModelID.size() + reserveInfo.numInstances);
            _instanceIDToBodyID.reserve(_instanceIDToBodyID.size() + reserveInfo.numInstances);
            _instanceIDToEntityID.reserve(_instanceIDToEntityID.size() + reserveInfo.numInstances);

            // Have ModelRenderer prepare all buffers for what we need to load
            _modelRenderer->Reserve(reserveInfo);
            animationSystem->Reserve(reserveInfo.numModels);

            // Create entt entities
            entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;

            size_t createdEntitiesOffset = _createdEntities.size();
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

            std::atomic<u32> numCreatedInstances = 0;
            //enki::TaskSet loadModelsTask(numDequeued, [&](enki::TaskSetPartition range, u32 threadNum)
            //{
            	//for (u32 i = range.start; i < range.end; i++)
                for (u32 i = 0; i < numDequeued; i++)
                {
                    LoadRequestInternal& request = _staticLoadRequests[i];

                    u32 placementHash = request.placement.nameHash;
                    if (!_nameHashToDiscoveredModel.contains(placementHash))
                    {
                        // Maybe we should add a warning that we tried to load a model that wasn't discovered? Or load an error cube or something?
                        continue;
                    }

                    std::mutex* mutex = _nameHashToLoadingMutex[placementHash];
                    std::scoped_lock lock(*mutex);

                    LoadState loadState = _nameHashToLoadState[placementHash];

                    if (loadState == LoadState::Failed)
                        continue;

                    if (loadState == LoadState::Received)
                    {
                        loadState = LoadState::Loading;
                        _nameHashToLoadState[placementHash] = LoadState::Loading;

                        bool didLoad = LoadRequest(request);

                        loadState = static_cast<LoadState>((LoadState::Loaded * didLoad) + (LoadState::Failed * !didLoad));;
                        _nameHashToLoadState[placementHash] = loadState;

                        if (!didLoad)
                            continue;
                    }

                    if (request.placement.uniqueID != std::numeric_limits<u32>().max() && _uniqueIDToinstanceID.contains(request.placement.uniqueID))
                        continue;

                    u32 index = static_cast<u32>(createdEntitiesOffset) + numCreatedInstances.fetch_add(1);
                    AddStaticInstance(_createdEntities[index], request);
                }
            //});

            // Execute the multithreaded job
            //taskScheduler->AddTaskSetToPipe(&loadModelsTask);
            //taskScheduler->WaitforTask(&loadModelsTask);

            // Destroy the entities we didn't use
            u32 numCreated = numCreatedInstances.load();
            registry->destroy(begin + numCreated, _createdEntities.end());
            _createdEntities.resize(createdEntitiesOffset + numCreated);

            // Fit the buffers to the data we loaded
            _modelRenderer->FitBuffersAfterLoad();
        }
    }

    size_t dynamicRequests = _dynamicRequests.size_approx();
    if (dynamicRequests > 0)
    {
        u32 numDequeued = static_cast<u32>(_dynamicRequests.try_dequeue_bulk(&_dynamicLoadRequests[0], MAX_DYNAMIC_LOADS_PER_FRAME));
        if (numDequeued > 0)
        {
            ModelRenderer::ReserveInfo reserveInfo;

            std::vector<u32> unloadRequests;
            unloadRequests.reserve(16);

            for (u32 i = 0; i < numDequeued; i++)
            {
                LoadRequestInternal& request = _dynamicLoadRequests[i];
                u32 nameHash = request.placement.nameHash;

                if (nameHash == std::numeric_limits<u32>().max())
                {
                    unloadRequests.push_back(i);
                    continue;
                }

                if (!_nameHashToDiscoveredModel.contains(nameHash))
                {
                    NC_LOG_ERROR("ModelLoader : Tried to load model with hash ({0}) which wasn't discovered", nameHash);
                    continue;
                }

                DiscoveredModel& discoveredModel = _nameHashToDiscoveredModel[nameHash];
                bool isSupported = discoveredModel.modelHeader.numVertices > 0;

                // Only increment Instance Count & Drawcall Count if the model have vertices
                {
                    reserveInfo.numInstances += 1 * isSupported;
                    reserveInfo.numOpaqueDrawcalls += discoveredModel.modelHeader.numOpaqueRenderBatches * isSupported;
                    reserveInfo.numTransparentDrawcalls += discoveredModel.modelHeader.numTransparentRenderBatches * isSupported;
                    reserveInfo.numBones += discoveredModel.modelHeader.numBones * isSupported;
                    reserveInfo.numTextureTransforms += discoveredModel.modelHeader.numTextureTransforms * isSupported;
                }

                bool hasDisplayID = request.displayID != std::numeric_limits<u32>().max();

                if (!_nameHashToLoadState.contains(nameHash))
                {
                    _nameHashToLoadState[nameHash] = LoadState::Received;
                    _nameHashToModelID[nameHash] = 0; // 0 should be a cube representing currently loading or something
                    _nameHashToLoadingMutex[nameHash] = new std::mutex();

                    reserveInfo.numModels++;
                    reserveInfo.numVertices += discoveredModel.modelHeader.numVertices * isSupported;
                    reserveInfo.numIndices += discoveredModel.modelHeader.numIndices * isSupported;
                    reserveInfo.numTextureUnits += discoveredModel.modelHeader.numTextureUnits + (discoveredModel.modelHeader.numTextureUnits * hasDisplayID) * isSupported;
                    reserveInfo.numDecorationSets += discoveredModel.modelHeader.numDecorationSets * isSupported;
                    reserveInfo.numDecorations += discoveredModel.modelHeader.numDecorations * isSupported;

                    reserveInfo.numUniqueOpaqueDrawcalls += discoveredModel.modelHeader.numOpaqueRenderBatches * isSupported;
                    reserveInfo.numUniqueTransparentDrawcalls += discoveredModel.modelHeader.numTransparentRenderBatches * isSupported;
                }
                else
                {
                    reserveInfo.numTextureUnits += discoveredModel.modelHeader.numTextureUnits * isSupported * hasDisplayID;
                }
            }

            // Prepare lookup tables
            _nameHashToModelID.reserve(_nameHashToModelID.size() + reserveInfo.numModels);
            _modelIDToNameHash.reserve(_modelIDToNameHash.size() + reserveInfo.numModels);
            _modelIDToComplexModel.reserve(_modelIDToComplexModel.size() + reserveInfo.numModels);
            _modelIDToAABB.reserve(_modelIDToAABB.size() + reserveInfo.numModels);
            _modelIDToAABB.reserve(_modelIDToAABB.size() + reserveInfo.numModels);
            _nameHashToLoadingMutex.reserve(_nameHashToLoadingMutex.size() + reserveInfo.numModels);

            _nameHashToJoltShape.reserve(_nameHashToJoltShape.size() + reserveInfo.numInstances);
            _uniqueIDToinstanceID.reserve(_uniqueIDToinstanceID.size() + reserveInfo.numInstances);
            _instanceIDToModelID.reserve(_instanceIDToModelID.size() + reserveInfo.numInstances);
            _instanceIDToBodyID.reserve(_instanceIDToBodyID.size() + reserveInfo.numInstances);
            _instanceIDToEntityID.reserve(_instanceIDToEntityID.size() + reserveInfo.numInstances);

            // Have ModelRenderer prepare all buffers for what we need to load
            _modelRenderer->Reserve(reserveInfo);
            animationSystem->Reserve(reserveInfo.numModels);

            for (u32 i = 0; i < numDequeued; i++)
            {
                LoadRequestInternal& request = _dynamicLoadRequests[i];

                if (request.entity == entt::null)
                    continue;

                u32 modelHash = request.placement.nameHash;
                if (!_nameHashToDiscoveredModel.contains(modelHash))
                {
                    // Maybe we should add a warning that we tried to load a model that wasn't discovered? Or load an error cube or something?
                    continue;
                }

                std::mutex* mutex = _nameHashToLoadingMutex[modelHash];
                std::scoped_lock lock(*mutex);

                LoadState loadState = _nameHashToLoadState[modelHash];

                if (loadState == LoadState::Failed)
                    continue;

                if (loadState == LoadState::Received)
                {
                    loadState = LoadState::Loading;
                    _nameHashToLoadState[modelHash] = LoadState::Loading;

                    bool didLoad = LoadRequest(request);

                    loadState = static_cast<LoadState>((LoadState::Loaded * didLoad) + (LoadState::Failed * !didLoad));;
                    _nameHashToLoadState[modelHash] = loadState;

                    if (!didLoad)
                        continue;
                }

                AddDynamicInstance(request.entity, request);
            }

            // Fit the buffers to the data we loaded
            _modelRenderer->FitBuffersAfterLoad();
        }
    }

    size_t unloadRequests = _unloadRequests.size_approx();
    if (unloadRequests)
    {
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        mat4x4 identity = mat4x4(1.0f);

        UnloadRequest unloadRequest;
        while (_unloadRequests.try_dequeue(unloadRequest))
        {
            _modelRenderer->ModifyInstance(unloadRequest.entity, unloadRequest.instanceID, std::numeric_limits<u32>().max(), nullptr, identity);
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
    loadRequest.entity = entt::null;
    loadRequest.placement = placement;

    _staticRequests.enqueue(loadRequest);
}

void ModelLoader::LoadDecoration(u32 instanceID, const Model::ComplexModel::Decoration& decoration)
{
    LoadRequestInternal loadRequest;
    loadRequest.entity = entt::null;
    loadRequest.instanceID = instanceID;

    Terrain::Placement placement;
    placement.nameHash = decoration.nameID;
    placement.position = decoration.position;
    placement.rotation = decoration.rotation;
    placement.scale = static_cast<u16>(decoration.scale * 1024.0f);

    loadRequest.placement = placement;

    _staticRequests.enqueue(loadRequest);
}

void ModelLoader::LoadModelForEntity(entt::entity entity, u32 modelNameHash)
{
    LoadRequestInternal loadRequest;
    loadRequest.entity = entity;
    loadRequest.placement.nameHash = modelNameHash;

    _dynamicRequests.enqueue(loadRequest);
}

bool ModelLoader::LoadDisplayIDForEntity(entt::entity entity, u32 displayID)
{
    LoadRequestInternal loadRequest;
    loadRequest.entity = entity;
    loadRequest.displayID = displayID;

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
    auto& clientDBCollection = registry->ctx().get<ECS::Singletons::ClientDBCollection>();
    auto* creatureDisplayInfoStorage = clientDBCollection.Get<ClientDB::Definitions::CreatureDisplayInfo>(ECS::Singletons::ClientDBHash::CreatureDisplayInfo);
    auto* creatureModelDataStorage = clientDBCollection.Get<ClientDB::Definitions::CreatureModelData>(ECS::Singletons::ClientDBHash::CreatureModelData);

    if (!creatureDisplayInfoStorage || !creatureModelDataStorage)
        return false;

    const ClientDB::Definitions::CreatureDisplayInfo* creatureDisplayInfo = creatureDisplayInfoStorage->GetRow(displayID);
    if (!creatureDisplayInfo)
        return false;

    const ClientDB::Definitions::CreatureModelData* creatureModelData = creatureModelDataStorage->GetRow(creatureDisplayInfo->modelID);
    if (!creatureModelData)
        return false;

    loadRequest.placement.nameHash = creatureModelData->modelHash;
    _dynamicRequests.enqueue(loadRequest);
    return true;
}

void ModelLoader::UnloadModelForEntity(entt::entity entity, u32 instanceID)
{
    UnloadRequest unloadRequest =
    {
        .entity = entity,
        .instanceID = instanceID
    };

    _unloadRequests.enqueue(unloadRequest);
}

u32 ModelLoader::GetModelHashFromModelPath(const std::string& modelPath)
{
    u32 nameHash = StringUtils::fnv1a_32(modelPath.c_str(), modelPath.length());

    if (!_nameHashToDiscoveredModel.contains(nameHash))
    {
        NC_LOG_ERROR("Failed to find DiscoveredModel for Model ({0})", modelPath);
        return std::numeric_limits<u32>().max();
    }

    return nameHash;
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
    return _nameHashToDiscoveredModel.contains(modelNameHash);
}

ModelLoader::DiscoveredModel& ModelLoader::GetDiscoveredModelFromModelID(u32 modelID)
{
    if (!_modelIDToNameHash.contains(modelID))
    {
        NC_LOG_CRITICAL("ModelLoader : Tried to access DiscoveredModel of invalid ModelID {0}", modelID);
    }

    u32 nameHash = _modelIDToNameHash[modelID];
    if (!_nameHashToDiscoveredModel.contains(nameHash))
    {
        NC_LOG_CRITICAL("ModelLoader : Tried to access DiscoveredModel of invalid NameHash {0}", nameHash);
    }

    return _nameHashToDiscoveredModel[nameHash];
}

bool ModelLoader::LoadRequest(const LoadRequestInternal& request)
{
    if (!_nameHashToDiscoveredModel.contains(request.placement.nameHash))
    {
        NC_LOG_ERROR("ModelLoader : Tried to load model nameHash ({0}) that doesn't exist", request.placement.nameHash);
        return false;
    }

    DiscoveredModel& discoveredModel = _nameHashToDiscoveredModel[request.placement.nameHash];

    fs::path path = complexModelPath / discoveredModel.name;
    FileReader cModelFile(path.string());
    if (!cModelFile.Open())
    {
        NC_LOG_CRITICAL("ModelLoader : Failed to open CModel file: {0}", path.string());
        return false;
    }

    // Load the file into memory
    size_t fileSize = cModelFile.Length();
    std::shared_ptr<Bytebuffer> cModelBuffer = Bytebuffer::BorrowRuntime(fileSize);

    cModelFile.Read(cModelBuffer.get(), fileSize);
    cModelFile.Close();

    // Extract the ComplexModel from the file
    Model::ComplexModel* model = new Model::ComplexModel();
    Model::ComplexModel::Read(cModelBuffer, *model);

    if (model->modelHeader.numVertices == 0)
    {
        NC_LOG_ERROR("ModelLoader : Tried to load model ({0}) without any vertices", discoveredModel.name);
        return false;
    }

    assert(discoveredModel.modelHeader.numVertices == model->vertices.size());

    u32 modelID = _modelRenderer->LoadModel(path.string(), *model);
    _nameHashToModelID[request.placement.nameHash] = modelID;

    _modelIDToComplexModel[modelID] = model;
    _modelIDToNameHash[modelID] = request.placement.nameHash;

    ECS::Components::AABB& aabb = _modelIDToAABB[modelID];
    aabb.centerPos = model->aabbCenter;
    aabb.extents = model->aabbExtents;

    // Generate Jolt Shape
    {
        i32 physicsEnabled = *CVarSystem::Get()->GetIntCVar(CVarCategory::Client | CVarCategory::Physics, "enabled"_h);
        u32 numPhysicsBytes = static_cast<u32>(model->physicsData.size());

        if (physicsEnabled && numPhysicsBytes > 0)
        {
            std::shared_ptr<Bytebuffer> physicsBuffer = std::make_shared<Bytebuffer>(model->physicsData.data(), numPhysicsBytes);
            JoltStreamIn streamIn(physicsBuffer);

            JPH::Shape::IDToShapeMap shapeMap;
            JPH::Shape::IDToMaterialMap materialMap;

            JPH::MeshShapeSettings::ShapeResult shapeResult = JPH::Shape::sRestoreWithChildren(streamIn, shapeMap, materialMap);

            _nameHashToJoltShape[request.placement.nameHash] = shapeResult.Get();
            discoveredModel.hasShape = true;
        }
    }

    Animation::AnimationSystem* animationSystem = ServiceLocator::GetAnimationSystem();
    animationSystem->AddSkeleton(modelID, *model);

    return true;
}

void ModelLoader::AddStaticInstance(entt::entity entityID, const LoadRequestInternal& request)
{
    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
    auto& tSystem = ECS::TransformSystem::Get(*registry);

    f32 scale = static_cast<f32>(request.placement.scale) / 1024.0f;
    tSystem.SetLocalTransform(entityID, request.placement.position, request.placement.rotation, vec3(scale, scale, scale));

    auto& name = registry->get<ECS::Components::Name>(entityID);
    DiscoveredModel& discoveredModel = _nameHashToDiscoveredModel[request.placement.nameHash];
    name.name = StringUtils::GetFileNameFromPath(discoveredModel.name);
    name.fullName = discoveredModel.name;
    name.nameHash = discoveredModel.nameHash;

    u32 modelID = _nameHashToModelID[request.placement.nameHash];
    u32 instanceID = _modelRenderer->AddPlacementInstance(entityID, modelID, nullptr, request.placement);

    auto& model = registry->get<ECS::Components::Model>(entityID);
    model.modelID = modelID;
    model.instanceID = instanceID;

    const ECS::Components::AABB& modelAABB = _modelIDToAABB[modelID];

    auto& aabb = registry->get<ECS::Components::AABB>(entityID);
    aabb.centerPos = modelAABB.centerPos;
    aabb.extents = modelAABB.extents;

    std::scoped_lock lock(_instanceIDToModelIDMutex);
    _uniqueIDToinstanceID[request.placement.uniqueID] = instanceID;
    _instanceIDToModelID[instanceID] = modelID;
    _instanceIDToEntityID[instanceID] = entityID;

    bool hasParent = request.instanceID != std::numeric_limits<u32>().max() && _instanceIDToEntityID.contains(request.instanceID);
    if (hasParent)
    {
        entt::entity parentEntityID = _instanceIDToEntityID[request.instanceID];
        tSystem.ParentEntityTo(parentEntityID, entityID);
    }

    if (discoveredModel.hasShape)
    {
        i32 physicsEnabled = *CVarSystem::Get()->GetIntCVar(CVarCategory::Client | CVarCategory::Physics, "enabled"_h);

        if (physicsEnabled)
        {
            entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
            auto& joltState = registry->ctx().get<ECS::Singletons::JoltState>();
            JPH::BodyInterface& bodyInterface = joltState.physicsSystem.GetBodyInterface();

            const JPH::ShapeRefC& shape = _nameHashToJoltShape[request.placement.nameHash];

            // TODO: We need to scale the shape

            auto& transform = registry->get<ECS::Components::Transform>(entityID);
            vec3 position = transform.GetWorldPosition();
            const quat& rotation = transform.GetWorldRotation();

            // Create the settings for the body itself. Note that here you can also set other properties like the restitution / friction.
            JPH::BodyCreationSettings bodySettings(new JPH::ScaledShapeSettings(shape, JPH::Vec3::sReplicate(scale)), JPH::RVec3(position.x, position.y, position.z), JPH::Quat(rotation.x, rotation.y, rotation.z, rotation.w), JPH::EMotionType::Static, Jolt::Layers::NON_MOVING);

            // Create the actual rigid body
            JPH::Body* body = bodyInterface.CreateBody(bodySettings); // Note that if we run out of bodies this can return nullptr
            if (body)
            {
                JPH::BodyID bodyID = body->GetID();

                // Store the entity ID in the body so we can look it up later
                body->SetUserData(static_cast<JPH::uint64>(entityID));

                bodyInterface.AddBody(bodyID, JPH::EActivation::Activate);
                _instanceIDToBodyID[instanceID] = bodyID.GetIndexAndSequenceNumber();
            }
        }
    }

    Animation::AnimationSystem* animationSystem = ServiceLocator::GetAnimationSystem();

    if (animationSystem->HasSkeleton(modelID))
    {
        auto& animationInitData = registry->get<ECS::Components::AnimationInitData>(entityID);
        animationInitData.flags.shouldInitialize = true;
        animationInitData.flags.isDynamic = false;
    }
}

void ModelLoader::AddDynamicInstance(entt::entity entityID, const LoadRequestInternal& request)
{
    Animation::AnimationSystem* animationSystem = ServiceLocator::GetAnimationSystem();
    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;

    DiscoveredModel& discoveredModel = _nameHashToDiscoveredModel[request.placement.nameHash];
    auto& name = registry->get<ECS::Components::Name>(entityID);
    name.name = StringUtils::GetFileNameFromPath(discoveredModel.name);
    name.fullName = discoveredModel.name;
    name.nameHash = discoveredModel.nameHash;

    auto& model = registry->get<ECS::Components::Model>(entityID);

    u32 modelID = _nameHashToModelID[request.placement.nameHash];
    Model::ComplexModel* complexModel = _modelIDToComplexModel[modelID];
    u32 instanceID = model.instanceID;

    auto& transform = registry->get<ECS::Components::Transform>(entityID);
    if (instanceID == std::numeric_limits<u32>().max())
    {
        instanceID = _modelRenderer->AddInstance(entityID, modelID, complexModel, transform.GetMatrix(), request.displayID);
    }
    else
    {
        _modelRenderer->ModifyInstance(entityID, instanceID, modelID, complexModel, transform.GetMatrix(), request.displayID);
    }

    model.modelID = modelID;
    model.instanceID = instanceID;

    const ECS::Components::AABB& modelAABB = _modelIDToAABB[modelID];
    auto& aabb = registry->get<ECS::Components::AABB>(entityID);
    aabb.centerPos = modelAABB.centerPos;
    aabb.extents = modelAABB.extents;

    std::scoped_lock lock(_instanceIDToModelIDMutex);
    _instanceIDToModelID[instanceID] = modelID;
    _instanceIDToEntityID[instanceID] = entityID;

    if (discoveredModel.hasShape)
    {
        i32 physicsEnabled = *CVarSystem::Get()->GetIntCVar(CVarCategory::Client | CVarCategory::Physics, "enabled"_h);

        if (physicsEnabled && registry->all_of<ECS::Components::NetworkedEntity>(entityID))
        {
            entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
            auto& joltState = registry->ctx().get<ECS::Singletons::JoltState>();
            JPH::BodyInterface& bodyInterface = joltState.physicsSystem.GetBodyInterface();

            const JPH::ShapeRefC& shape = _nameHashToJoltShape[request.placement.nameHash];

            // TODO: We need to scale the shape

            auto& transform = registry->get<ECS::Components::Transform>(entityID);
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
                _instanceIDToBodyID[instanceID] = bodyID.GetIndexAndSequenceNumber();
            }
        }
    }

    if (animationSystem->HasSkeleton(modelID))
    {
        auto& animationInitData = registry->get_or_emplace<ECS::Components::AnimationInitData>(entityID);
        animationInitData.flags.shouldInitialize = true;
        animationInitData.flags.isDynamic = true;

        if (_modelRenderer)
        {
            _modelRenderer->AddAnimationInstance(instanceID);
        }
    }
}
