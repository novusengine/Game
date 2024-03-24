#pragma once
#include <Game/ECS/Components/AABB.h>

#include <Base/Types.h>
#include <Base/Container/ConcurrentQueue.h>
#include <Base/Container/SafeUnorderedMap.h>

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

class ModelRenderer;
class ModelLoader
{
public:
    static constexpr u32 MAX_STATIC_LOADS_PER_FRAME = 65535;
    static constexpr u32 MAX_DYNAMIC_LOADS_PER_FRAME = 1024;

    enum LoadState
    {
        Received,
        Loading,
        Loaded,
        Failed
    };

    struct DiscoveredModel
    {
        std::string name;
        u32 nameHash;

        bool hasShape;
        Model::ComplexModel::ModelHeader modelHeader;
    };

private:
    struct LoadRequestInternal
    {
    public:
        entt::entity entity;
        u32 instanceID = std::numeric_limits<u32>().max();
        
        Terrain::Placement placement;
    };

public:
    ModelLoader(ModelRenderer* modelRenderer);

    void Init();
    void Clear();
    void Update(f32 deltaTime);

    entt::entity CreateModelEntity(const std::string& name);

    void LoadPlacement(const Terrain::Placement& placement);
    void LoadDecoration(u32 instanceID, const Model::ComplexModel::Decoration& decoration);
    void LoadModelForEntity(entt::entity entity, u32 modelNameHash);

    void UnloadModelForEntity(entt::entity entity, u32 modelID);

    u32 GetModelHashFromModelPath(const std::string& modelPath);
    bool GetModelIDFromInstanceID(u32 instanceID, u32& modelID);
    bool GetEntityIDFromInstanceID(u32 instanceID, entt::entity& entityID);

    bool ContainsDiscoveredModel(u32 modelNameHash);
    DiscoveredModel& GetDiscoveredModelFromModelID(u32 modelID);

private:
    bool LoadRequest(const LoadRequestInternal& request);
    void AddStaticInstance(entt::entity entityID, const LoadRequestInternal& request);
    void AddDynamicInstance(entt::entity entityID, const LoadRequestInternal& request);

private:
    ModelRenderer* _modelRenderer = nullptr;
    std::vector<LoadRequestInternal> _staticLoadRequests;
    moodycamel::ConcurrentQueue<LoadRequestInternal> _staticRequests;

    std::vector<LoadRequestInternal> _dynamicLoadRequests;
    moodycamel::ConcurrentQueue<LoadRequestInternal> _dynamicRequests;

    robin_hood::unordered_map<u32, LoadState> _nameHashToLoadState;
    robin_hood::unordered_map<u32, u32> _nameHashToModelID;
    robin_hood::unordered_map<u32, JPH::ShapeRefC> _nameHashToJoltShape;
    robin_hood::unordered_map<u32, DiscoveredModel> _nameHashToDiscoveredModel;
    robin_hood::unordered_map<u32, std::mutex*> _nameHashToLoadingMutex;

    robin_hood::unordered_map<u32, u32> _uniqueIDToinstanceID;
    robin_hood::unordered_map<u32, u32> _instanceIDToModelID;
    robin_hood::unordered_map<u32, u32> _instanceIDToBodyID;
    robin_hood::unordered_map<u32, entt::entity> _instanceIDToEntityID;
    std::mutex _instanceIDToModelIDMutex;

    robin_hood::unordered_map<u32, u32> _modelIDToNameHash;
    robin_hood::unordered_map<u32, ECS::Components::AABB> _modelIDToAABB;

    std::vector<entt::entity> _createdEntities;
};