#pragma once

#include "Game-Lib/Rendering/Asset/RenderAssetHandles.h"
#include "Game-Lib/Rendering/Model/Asset/ModelAssetRegistry.h"
#include "Game-Lib/Rendering/Scene/RenderSceneHandles.h"

#include <functional>
#include <robinhood/robinhood.h>
#include <vector>

namespace ModelLoading
{
    class ModelGeometryStorage;
}

namespace RenderScenes
{
    class RenderScene;
}

namespace ModelScene
{
    using ResolveEmbeddedModelCallback =
        std::function<ModelLoading::EmbeddedModelLoadStatus(FileFormat::AssetID, RenderAssets::ModelHandle&)>;

    struct SpawnedEmbeddedInstance
    {
        RenderScenes::ModelInstanceHandle handle;
        u32 sourceIndex = 0;
    };

    struct SpawnedEmbeddedInstances
    {
        RenderAssets::ModelHandle parentModel;
        std::vector<SpawnedEmbeddedInstance> instances;
        u32 selectedSet = 0;
        bool hasSets = false;
    };

    struct EmbeddedInstanceSpawnerStats
    {
        u32 spawnedInstances = 0;
        u32 invalidReferenceSkips = 0;
        u32 missingGeometrySkips = 0;
        u32 unexpectedDependencyFailures = 0;
        u32 invalidSetSelections = 0;
    };

    struct EmbeddedInstanceSpawnCursor
    {
        mat4x4 parentWorld = mat4x4(1.0f);
        u32 nextSourceIndex = 0;
        bool active = false;
    };

    enum class EmbeddedInstanceSpawnStatus : u8
    {
        InProgress,
        Complete,
        Failed
    };

    // Expands CPU-side embedded model definitions into GPU-backed RenderScene instances.
    // It preserves authored parent-child transforms and lets doodad-set changes reuse existing child instances.
    class EmbeddedInstanceSpawner
    {
      public:
        EmbeddedInstanceSpawner(const ModelLoading::ModelGeometryStorage* geometryStorage,
                                RenderScenes::RenderScene* scene, ResolveEmbeddedModelCallback resolveModel);

        bool Spawn(RenderAssets::ModelHandle parentModel, const mat4x4& parentWorld, u32 requestedSet,
                   SpawnedEmbeddedInstances& outInstances);
        bool BeginSpawn(RenderAssets::ModelHandle parentModel, const mat4x4& parentWorld,
                        u32 requestedSet, SpawnedEmbeddedInstances& outInstances,
                        EmbeddedInstanceSpawnCursor& cursor);
        EmbeddedInstanceSpawnStatus ContinueSpawn(EmbeddedInstanceSpawnCursor& cursor,
                                                  SpawnedEmbeddedInstances& instances,
                                                  u32 maxInstances, u32& outProcessed);
        bool SetInstanceSet(SpawnedEmbeddedInstances& instances, u32 requestedSet);
        void Destroy(SpawnedEmbeddedInstances& instances, u64 retireValue);

        const EmbeddedInstanceSpawnerStats& GetStats() const { return _stats; }

      private:
        bool IsEnabled(const SpawnedEmbeddedInstances& instances, u32 sourceIndex) const;
        u32 SelectSet(RenderAssets::ModelHandle parentModel, u32 requestedSet, bool& outValid) const;

        const ModelLoading::ModelGeometryStorage* _geometryStorage = nullptr;
        RenderScenes::RenderScene* _scene = nullptr;
        ResolveEmbeddedModelCallback _resolveModel;
        robin_hood::unordered_set<FileFormat::AssetID> _reportedDependencyFailures;
        EmbeddedInstanceSpawnerStats _stats;
    };
}
