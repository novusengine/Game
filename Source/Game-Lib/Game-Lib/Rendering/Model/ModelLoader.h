#pragma once
#include "Game-Lib/Application/IOLoader.h"
#include "Game-Lib/ECS/Components/AABB.h"

#include <Base/Types.h>
#include <Base/Container/ConcurrentQueue.h>
#include <Base/Container/SafeUnorderedMap.h>

#include <FileFormat/Novus/ClientDB/Definitions.h>
#include <FileFormat/Novus/Map/MapChunk.h>
#include <FileFormat/Novus/Model/ComplexModel.h>

#include <entt/entt.hpp>
#include <enkiTS/TaskScheduler.h>
#include <Jolt/Jolt.h>
#include <Jolt/Geometry/Triangle.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <robinhood/robinhood.h>
#include <type_safe/strong_typedef.hpp>

namespace ECS::Components
{
    struct Model;
}

class ModelRenderer;
class ModelLoader
{
public:
    static constexpr u32 MAX_PENDING_LOADS_PER_FRAME = 4096;
    static constexpr u32 MAX_INTERNAL_LOADS_PER_FRAME = 4096;

    enum LoadState : u8
    {
        NotLoaded,
        Requested,
        Received,
        Loaded,
        Failed
    };

    struct DiscoveredModel
    {
    public:
        std::string name;
        u32 modelHash;
        bool hasShape;
        Model::ComplexModel* model;
    };

    struct UnloadRequest
    {
    public:
        entt::entity entity;
        u32 instanceID;
    };

private:
    enum class LoadRequestType : u8
    {
        Invalid = 0,
        Placement,
        Decoration,
        Model,
        DisplayID
    };

    struct LoadRequestInternal
    {
    public:
        LoadRequestType type = LoadRequestType::Invalid;
        entt::entity entity = entt::null;
        u32 instanceID = std::numeric_limits<u32>().max();
        
        u32 uniqueID = std::numeric_limits<u32>().max();
        u32 modelHash = std::numeric_limits<u32>().max();
        u32 extraData = std::numeric_limits<u32>().max();

        vec3 spawnPosition = vec3(0.0f, 0.0f, 0.0f);
        quat spawnRotation = quat(1.0f, 0.0f, 0.0f, 0.0f);
        f32 scale = 1.0f;
    };

    struct WorkRequest
    {
    public:
        std::string path;
        u32 modelHash = std::numeric_limits<u32>().max();
        std::shared_ptr<Bytebuffer> data = nullptr;
    };

public:
    ModelLoader(ModelRenderer* modelRenderer);

    void Init();
    void Clear();
    void Update(f32 deltaTime);

    entt::entity CreateModelEntity(const std::string& name);

public: // Load Request Helpers
    void LoadPlacement(const Terrain::Placement& placement);
    void LoadDecoration(u32 instanceID, const Model::ComplexModel::Decoration& decoration);
    bool LoadModelForEntity(entt::entity entity, ECS::Components::Model& model, u32 modelNameHash);
    bool LoadDisplayIDForEntity(entt::entity entity, ECS::Components::Model& model, ClientDB::Definitions::DisplayInfoType displayInfoType, u32 displayID);
    void UnloadModelForEntity(entt::entity entity, ECS::Components::Model& model);

public:
    const Model::ComplexModel* GetModelInfo(u32 modelHash);
    u32 GetModelHashFromModelPath(const std::string& modelPath);
    bool GetModelIDFromInstanceID(u32 instanceID, u32& modelID);
    bool GetEntityIDFromInstanceID(u32 instanceID, entt::entity& entityID);

    bool ContainsDiscoveredModel(u32 modelNameHash);
    DiscoveredModel& GetDiscoveredModelFromModelID(u32 modelID);

    bool DiscoveredModelsComplete() { return _discoveredModelsComplete; }

private:
    bool LoadRequest(DiscoveredModel& discoveredModel);
    void AddStaticInstance(entt::entity entityID, const LoadRequestInternal& request);
    void AddDynamicInstance(entt::entity entityID, const LoadRequestInternal& request);

    void HandleDiscoverModelCallback(bool result, std::shared_ptr<Bytebuffer> buffer, const std::string& path, u64 userdata);

private:
    ModelRenderer* _modelRenderer = nullptr;

    std::atomic<u32> _numDiscoveredModelsToLoad = 0;
    u32 _numDiscoveredModelsLoaded = 0;
    bool _discoveredModelsComplete = false;

    moodycamel::ConcurrentQueue<WorkRequest> _discoveredModelPendingWorkRequests;

    std::vector<LoadRequestInternal> _pendingLoadRequestsVector;
    moodycamel::ConcurrentQueue<LoadRequestInternal> _pendingLoadRequests;

    std::vector<LoadRequestInternal> _internalLoadRequestsVector;
    moodycamel::ConcurrentQueue<LoadRequestInternal> _internalLoadRequests; // Ready to add instance (Model is already loaded)
    moodycamel::ConcurrentQueue<UnloadRequest> _unloadRequests;
    moodycamel::ConcurrentQueue<DiscoveredModel> _discoveredModels;

    robin_hood::unordered_map<u32, LoadState> _modelHashToLoadState;
    robin_hood::unordered_map<u32, u32> _modelHashToModelID;
    robin_hood::unordered_map<u32, JPH::ShapeRefC> _modelHashToJoltShape;
    robin_hood::unordered_map<u32, DiscoveredModel> _modelHashToDiscoveredModel;
    robin_hood::unordered_map<u32, std::mutex*> _modelHashToLoadingMutex;

    robin_hood::unordered_map<u32, u32> _uniqueIDToinstanceID;
    robin_hood::unordered_map<u32, u32> _instanceIDToModelID;
    robin_hood::unordered_map<u32, u32> _instanceIDToBodyID;
    robin_hood::unordered_map<u32, entt::entity> _instanceIDToEntityID;
    std::mutex _modelHashMutex;
    std::mutex _instanceIDToModelIDMutex;
    std::mutex _transformSystemMutex;
    std::mutex _physicsSystemMutex;
    std::mutex _animationMutex;

    robin_hood::unordered_map<u32, u32> _modelIDToModelHash;
    robin_hood::unordered_map<u32, ECS::Components::AABB> _modelIDToAABB;

    std::vector<entt::entity> _createdEntities;
};