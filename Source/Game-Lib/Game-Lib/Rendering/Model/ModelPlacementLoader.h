#pragma once

#include "Game-Lib/Rendering/Model/Scene/EmbeddedInstanceSpawner.h"

#include <Base/Container/ConcurrentQueue.h>

#include <FileFormat/Novus/Map/MapChunk.h>

#include <robinhood/robinhood.h>

#include <optional>

namespace PACT
{
    class PactStorage;
}

namespace RenderAssets
{
    class RenderAssetResources;
}

namespace RenderScenes
{
    class RenderScene;
}

namespace ModelRendering
{
    struct ModelPlacementLoaderStats
    {
        u32 livePlacements = 0;
        u32 duplicatePlacements = 0;
        u32 sourceResolutionFailures = 0;
        u32 modelFallbackPlacements = 0;
        u32 sourceAssetLookups = 0;
        u32 sourceAssetCacheEntries = 0;
        ModelScene::EmbeddedInstanceSpawnerStats embedded;
    };

    // Converts CPU-side map placement records into GPU-backed model Scene instances.
    // It owns map-placement lifetime and keeps embedded children synchronized with their parent placement.
    class ModelPlacementLoader
    {
      public:
        ModelPlacementLoader(PACT::PactStorage* pactStorage, RenderAssets::RenderAssetResources* assets,
                             RenderScenes::RenderScene* scene);

        void Reserve(u32 rootPlacementCount, u32 modelCount);
        void QueuePlacement(const Terrain::Placement& placement);
        void Update();
        bool SetDoodadSet(u32 uniqueID, u16 doodadSet);
        bool SetGeometryGroupEnabled(u32 uniqueID, u32 groupID, bool enabled);
        void Clear();
        bool IsLoading() const { return _numProcessedPlacements.load() != _numQueuedPlacements.load(); }
        f32 GetLoadingProgress() const;

        ModelPlacementLoaderStats GetStats() const;

      private:
        struct PlacementInstances
        {
            RenderScenes::ModelInstanceHandle root;
            ModelScene::SpawnedEmbeddedInstances embedded;
        };

        struct ActivePlacement
        {
            Terrain::Placement source;
            PlacementInstances instances;
            ModelScene::EmbeddedInstanceSpawnCursor embeddedCursor;
            ModelScene::EmbeddedInstanceSpawnerStats statsBefore;
        };

        struct WaitingPlacement
        {
            Terrain::Placement source;
            FileFormat::AssetID assetID = FileFormat::INVALID_ASSET_ID;
        };

        bool BeginPlacement(const Terrain::Placement& placement, FileFormat::AssetID assetID, RenderAssets::ModelHandle model);
        void FinishPlacement(bool succeeded);
        FileFormat::AssetID ResolveModelAssetID(u64 sourceReference);
        static mat4x4 MakeWorldTransform(const Terrain::Placement& placement);

        PACT::PactStorage* _pactStorage = nullptr;
        RenderAssets::RenderAssetResources* _assets = nullptr;
        RenderScenes::RenderScene* _scene = nullptr;
        ModelScene::EmbeddedInstanceSpawner _embeddedSpawner;
        moodycamel::ConcurrentQueue<Terrain::Placement> _pendingPlacements;
        robin_hood::unordered_map<u32, PlacementInstances> _placements;
        std::optional<ActivePlacement> _activePlacement;
        std::optional<WaitingPlacement> _waitingPlacement;
        robin_hood::unordered_map<u64, FileFormat::AssetID> _sourceAssets;
        robin_hood::unordered_set<u64> _reportedResolutionFailures;
        robin_hood::unordered_set<FileFormat::AssetID> _reportedFallbackAssets;
        robin_hood::unordered_set<RenderAssets::ModelHandle::type> _reportedExpectedSkipModels;
        robin_hood::unordered_set<RenderAssets::ModelHandle::type> _reportedDependencyFailureModels;
        u32 _duplicatePlacements = 0;
        u32 _sourceResolutionFailures = 0;
        u32 _modelFallbackPlacements = 0;
        u32 _sourceAssetLookups = 0;
        std::atomic<u32> _numQueuedPlacements = 0;
        std::atomic<u32> _numProcessedPlacements = 0;
    };
}
