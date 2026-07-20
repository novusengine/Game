#pragma once
#include "Game-Lib/ECS/Components/AABB.h"
#include "Game-Lib/Gameplay/Database/Unit.h"
#include "Game-Lib/Rendering/Model/ModelLoadTypes.h"

#include <Base/Types.h>
#include <Base/Container/ConcurrentQueue.h>
#include <Base/Container/SafeUnorderedMap.h>

#include <FileFormat/Novus/Map/MapChunk.h>
#include <FileFormat/Novus/Model/ComplexModel.h>

#include <Renderer/Descriptors/TextureDesc.h>

#include <entt/entt.hpp>
#include <enkiTS/TaskScheduler.h>
#include <Jolt/Jolt.h>
#include <Jolt/Geometry/Triangle.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <robinhood/robinhood.h>

#include <deque>

namespace ECS::Components
{
    struct Model;
}

class TerrainLoader;
class LightRenderer;
class ModelRenderer;
struct ActiveModelPrepareJob;
class ModelLoader
{
public:
    static constexpr u32 MAX_MAP_LOADS_PER_FRAME = 4096;
    static constexpr u32 MAX_GAMEPLAY_LOADS_PER_FRAME = 64;

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
        u64 modelHash;
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
        ModelLoading::ModelLoadRequestID requestID = ModelLoading::INVALID_MODEL_LOAD_REQUEST_ID;
        LoadRequestType type = LoadRequestType::Invalid;
        entt::entity entity = entt::null;

        u64 modelHash = std::numeric_limits<u64>().max();
        vec3 spawnPosition = vec3(0.0f, 0.0f, 0.0f);
        quat spawnRotation = quat(1.0f, 0.0f, 0.0f, 0.0f);
        f32 scale = 1.0f;

        u64 extraData1 = std::numeric_limits<u64>().max();
        u64 extraData2 = std::numeric_limits<u64>().max();
        u64 extraData3 = std::numeric_limits<u64>().max();
    };

    struct LoadRequestResultInternal
    {
    public:
        ModelLoading::ModelLoadRequestID requestID = ModelLoading::INVALID_MODEL_LOAD_REQUEST_ID;
        LoadRequestInternal request;
        bool success = false;
        bool isStatic = false;
    };

    struct ModelAssetRecord
    {
    public:
        LoadState loadState = LoadState::NotLoaded;
        std::vector<LoadRequestInternal> waitingRequests;
    };

    struct WorkRequest
    {
    public:
        std::string path;
        u64 modelHash = std::numeric_limits<u64>().max();
        std::shared_ptr<Bytebuffer> data = nullptr;
    };

public:
    ModelLoader(ModelRenderer* modelRenderer, LightRenderer* lightRenderer);

    void Init();
    void Shutdown();
    void Clear();
    void Update(f32 deltaTime);

    entt::entity CreateModelEntity(const std::string& name);

public: // Load Request Helpers
    void SetTerrainLoader(TerrainLoader* terrainLoader) { _terrainLoader = terrainLoader; }
    void SetTerrainLoading(bool loading) { _terrainLoading = loading; }

    f32 GetLoadingProgress() const;

    void LoadPlacement(const Terrain::Placement& placement);
    void LoadDecoration(u32 instanceID, const Model::ComplexModel::Decoration& decoration);
    bool LoadModelForEntity(entt::entity entity, ECS::Components::Model& model, u64 modelNameHash);
    bool LoadDisplayIDForEntity(entt::entity entity, ECS::Components::Model& model, Database::Unit::DisplayInfoType displayInfoType, u32 displayID, u64 modelHash = std::numeric_limits<u64>().max(), u8 modelVariant = 0);
    void UnloadModelForEntity(entt::entity entity, ECS::Components::Model& model);

    void SetEntityVisible(entt::entity entity, bool visible);
    void SetModelVisible(const ECS::Components::Model& model, bool visible);

    void SetEntityTransparent(entt::entity entity, bool transparent, f32 opacity);
    void SetModelTransparent(const ECS::Components::Model& model, bool transparent, f32 opacity);

    void SetEntityHighlight(entt::entity entity, f32 highlightIntensity);
    void SetModelHighlight(const ECS::Components::Model& model, f32 highlightIntensity);

    void EnableGroupForEntity(entt::entity entity, u32 groupID);
    void EnableGroupForModel(const ECS::Components::Model& model, u32 groupID);

    void DisableGroupForEntity(entt::entity entity, u32 groupID);
    void DisableGroupForModel(const ECS::Components::Model& model, u32 groupID);

    void DisableGroupsForEntity(entt::entity entity, u32 groupIDStart, u32 groupIDEnd);
    void DisableGroupsForModel(const ECS::Components::Model& model, u32 groupIDStart, u32 groupIDEnd);

    void DisableAllGroupsForEntity(entt::entity entity);
    void DisableAllGroupsForModel(const ECS::Components::Model& model);

    void SetSkinTextureForEntity(entt::entity entity, Renderer::TextureID textureID);
    void SetSkinTextureForModel(const ECS::Components::Model& model, Renderer::TextureID textureID);

    void SetHairTextureForEntity(entt::entity entity, Renderer::TextureID textureID);
    void SetHairTextureForModel(const ECS::Components::Model& model, Renderer::TextureID textureID);

public:
    const Model::ComplexModel* GetModelInfo(u64 modelHash);
    u64 GetModelHashFromModelPath(const std::string& modelPath);
    bool GetModelIDFromInstanceID(u32 instanceID, u32& modelID);
    bool GetEntityIDFromInstanceID(u32 instanceID, entt::entity& entityID);

    // Raw Jolt body id (IndexAndSequenceNumber) for an instance's collision body, if it has one.
    bool GetBodyIDFromInstanceID(u32 instanceID, u32& bodyID);

    bool ContainsDiscoveredModel(u64 modelHash);
    DiscoveredModel& GetDiscoveredModel(u64 modelHash);
    DiscoveredModel& GetDiscoveredModelFromModelID(u32 modelID);

private:
    bool LoadRequest(DiscoveredModel& discoveredModel);
    bool CommitPreparedModel(DiscoveredModel& discoveredModel, const ModelLoading::PreparedRenderModel& preparedModel);
    void ConsumePreparedModels();
    void ReapCompletedPrepareJobs();
    void DispatchAsyncLoadRequests(moodycamel::ConcurrentQueue<LoadRequestInternal>& workQueue, u32 numRequests);
    void CancelAndDrainPrepareJobs();
    void CompletePreparedRequest(const LoadRequestInternal& request, bool success);
    bool IsCurrentEntityRequest(const LoadRequestInternal& request) const;
    ModelLoading::ModelLoadRequestID GetNextLoadRequestID();
    void EnqueueLoadResult(const LoadRequestInternal& request, bool success, bool isStatic);
    void AddStaticInstance(entt::entity entityID, const LoadRequestInternal& request);
    void AddDynamicInstance(entt::entity entityID, const LoadRequestInternal& request);

private:
    TerrainLoader* _terrainLoader = nullptr;
    ModelRenderer* _modelRenderer = nullptr;
    LightRenderer* _lightRenderer = nullptr;

    bool _terrainLoading = false;
    std::atomic<u32> _numTerrainModelsToLoad = 0;
    u32 _numTerrainModelsLoaded = 0;

    moodycamel::ConcurrentQueue<WorkRequest> _discoveredModelPendingWorkRequests;

    moodycamel::ConcurrentQueue<LoadRequestResultInternal> _loadRequestResults; // Results carry stable IDs and owned request snapshots.
    moodycamel::ConcurrentQueue<ModelLoading::PreparedModelResult> _preparedModelResults; // Worker-produced, game-thread-consumed.
    std::deque<ModelLoading::PreparedModelResult> _pendingPreparedModelCommits; // Game-thread-only commit backlog.
    std::vector<LoadRequestInternal> _pendingLoadRequestsVector;
    moodycamel::ConcurrentQueue<LoadRequestInternal> _pendingTerrainLoadRequests;
    moodycamel::ConcurrentQueue<LoadRequestInternal> _pendingLoadRequests;

    std::vector<LoadRequestInternal> _internalLoadRequestsVector;
    moodycamel::ConcurrentQueue<LoadRequestInternal> _internalLoadRequests; // Ready to add instance (Model is already loaded)
    moodycamel::ConcurrentQueue<UnloadRequest> _unloadRequests;
    moodycamel::ConcurrentQueue<DiscoveredModel> _discoveredModels;

    robin_hood::unordered_map<u64, ModelAssetRecord> _modelAssets; // Game-thread-owned content state.
    robin_hood::unordered_map<u64, u32> _modelHashToModelID;
    robin_hood::unordered_map<u64, JPH::ShapeRefC> _modelHashToJoltShape;
    robin_hood::unordered_map<u64, DiscoveredModel> _modelHashToDiscoveredModel;

    robin_hood::unordered_map<u32, u32> _uniqueIDToinstanceID;
    robin_hood::unordered_map<u32, u32> _instanceIDToModelID;
    robin_hood::unordered_map<u32, u32> _instanceIDToBodyID;
    robin_hood::unordered_map<u32, entt::entity> _instanceIDToEntityID;
    robin_hood::unordered_map<u32, ModelLoading::ModelLoadRequestID> _entityToLatestRequestID;
    std::mutex _instanceIDToModelIDMutex;
    std::mutex _transformSystemMutex;
    std::mutex _physicsSystemMutex;
    std::mutex _animationMutex;

    robin_hood::unordered_map<u32, u64> _modelIDToModelHash;
    robin_hood::unordered_map<u32, ECS::Components::AABB> _modelIDToAABB;

    std::vector<entt::entity> _createdEntities;
    std::vector<std::unique_ptr<ActiveModelPrepareJob>> _activeModelPrepareJobs; // Game-thread-owned task lifetimes.
    std::atomic<ModelLoading::ModelLoadRequestID> _nextLoadRequestID = 1;
    u64 _loaderEpoch = 1;
};
