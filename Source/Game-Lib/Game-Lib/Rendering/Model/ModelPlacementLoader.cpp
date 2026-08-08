#include "ModelPlacementLoader.h"

#include "Game-Lib/Rendering/Asset/RenderAssetResources.h"
#include "Game-Lib/Rendering/Model/Asset/ModelGeometryStorage.h"
#include "Game-Lib/Rendering/Scene/RenderScene.h"
#include "Game-Lib/Util/AssetPath.h"

#include <Base/CVarSystem/CVarSystem.h>
#include <Base/Util/DebugHandler.h>

#include <Filesystem/PactStorage.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>

AutoCVar_Int CVAR_ModelPlacementLoadsPerFrame(
    CVarCategory::Client | CVarCategory::Rendering, "modelPlacementLoadsPerFrame",
    "maximum root or embedded Model V2 map placements loaded per frame", 2048, CVarFlags::None);

namespace ModelRendering
{
    ModelPlacementLoader::ModelPlacementLoader(PACT::PactStorage* pactStorage,
                                               RenderAssets::RenderAssetResources* assets,
                                               RenderScenes::RenderScene* scene)
        : _pactStorage(pactStorage), _assets(assets), _scene(scene),
          _embeddedSpawner(&assets->GetModelGeometryStorage(), scene,
                           [assets](FileFormat::AssetID assetID, RenderAssets::ModelHandle& outHandle) {
                               return assets->LoadEmbeddedModel(assetID, outHandle);
                           })
    {
        _placements.reserve(4096);
        _sourceAssets.reserve(1024);
    }

    void ModelPlacementLoader::Reserve(u32 rootPlacementCount, u32 modelCount)
    {
        _placements.reserve(std::max(_placements.size(), static_cast<size_t>(rootPlacementCount)));
        _sourceAssets.reserve(_sourceAssets.size() + modelCount);
    }

    void ModelPlacementLoader::QueuePlacement(const Terrain::Placement& placement)
    {
        _numQueuedPlacements.fetch_add(1, std::memory_order_relaxed);
        _pendingPlacements.enqueue(placement);
    }

    void ModelPlacementLoader::Update()
    {
        ZoneScopedN("ModelPlacementLoader::Update");
        TracyPlot("V2 Placement Queue", static_cast<i64>(_pendingPlacements.size_approx()));

        const u32 maxLoads = static_cast<u32>(std::max(1, CVAR_ModelPlacementLoadsPerFrame.Get()));
        u32 loaded = 0;
        while (loaded < maxLoads)
        {
            if (!_activePlacement)
            {
                Terrain::Placement placement;
                if (!_pendingPlacements.try_dequeue(placement))
                    break;
                ++loaded;
                if (!BeginPlacement(placement))
                {
                    _numProcessedPlacements.fetch_add(1, std::memory_order_relaxed);
                    continue;
                }
            }

            u32 embeddedLoaded = 0;
            const ModelScene::EmbeddedInstanceSpawnStatus status =
                _embeddedSpawner.ContinueSpawn(_activePlacement->embeddedCursor,
                                               _activePlacement->instances.embedded,
                                               maxLoads - loaded, embeddedLoaded);
            loaded += embeddedLoaded;
            if (status == ModelScene::EmbeddedInstanceSpawnStatus::InProgress)
                break;

            FinishPlacement(status == ModelScene::EmbeddedInstanceSpawnStatus::Complete);
            _numProcessedPlacements.fetch_add(1, std::memory_order_relaxed);
        }
        TracyPlot("V2 Placements Loaded", static_cast<i64>(loaded));
    }

    bool ModelPlacementLoader::BeginPlacement(const Terrain::Placement& placement)
    {
        ZoneScopedN("ModelPlacementLoader::BeginPlacement");

        if (_placements.contains(placement.uniqueID))
        {
            ++_duplicatePlacements;
            return false;
        }

        const FileFormat::AssetID assetID = ResolveModelAssetID(placement.nameHash);
        RenderAssets::ModelHandle model = _assets->GetFallbackModel();
        if (assetID != FileFormat::INVALID_ASSET_ID)
        {
            model = _assets->LoadModel(assetID);
            if (model == _assets->GetFallbackModel())
            {
                ++_modelFallbackPlacements;
                if (_reportedFallbackAssets.insert(assetID).second)
                {
                    const std::string* sourcePath = _pactStorage->GetFilePath(placement.nameHash);
                    NC_LOG_ERROR("MODEL_PLACEMENT root_fallback unique_id={} source={} path={} asset={}",
                                 placement.uniqueID, placement.nameHash,
                                 sourcePath ? *sourcePath : "<unresolved>", assetID);
                }
            }
        }
        else if (_reportedResolutionFailures.insert(placement.nameHash).second)
        {
            NC_LOG_ERROR("MODEL_PLACEMENT root_fallback source={} reason=source_path_unresolved", placement.nameHash);
        }

        const mat4x4 worldTransform = MakeWorldTransform(placement);
        RenderScenes::ModelInstanceDesc desc;
        desc.model = model;
        desc.worldTransform = worldTransform;
        const RenderScenes::ModelInstanceHandle root = _scene->CreateModelInstance(desc);
        if (!_scene->IsPending(root))
            return false;

        ActivePlacement active;
        active.source = placement;
        active.instances.root = root;
        active.statsBefore = _embeddedSpawner.GetStats();
        if (!_embeddedSpawner.BeginSpawn(model, worldTransform, placement.doodadSet,
                                         active.instances.embedded, active.embeddedCursor))
        {
            _scene->DestroyModelInstance(root, 0);
            return false;
        }

        _activePlacement = std::move(active);
        return true;
    }

    void ModelPlacementLoader::FinishPlacement(bool succeeded)
    {
        ZoneScopedN("ModelPlacementLoader::FinishPlacement");

        ActivePlacement active = std::move(*_activePlacement);
        _activePlacement.reset();
        if (!succeeded)
        {
            _embeddedSpawner.Destroy(active.instances.embedded, 0);
            _scene->DestroyModelInstance(active.instances.root, 0);
            return;
        }

        const ModelScene::EmbeddedInstanceSpawnerStats after = _embeddedSpawner.GetStats();
        const u32 invalidSkips = after.invalidReferenceSkips - active.statsBefore.invalidReferenceSkips;
        const u32 missingSkips = after.missingGeometrySkips - active.statsBefore.missingGeometrySkips;
        const RenderAssets::ModelHandle model = active.instances.embedded.parentModel;
        const RenderAssets::ModelHandle::type modelIndex =
            static_cast<RenderAssets::ModelHandle::type>(model);
        if ((invalidSkips != 0 || missingSkips != 0) && _reportedExpectedSkipModels.insert(modelIndex).second)
        {
            NC_LOG_INFO("MODEL_EMBEDDED expected_skips parent={} invalid_reference={} no_renderable_geometry={}",
                        modelIndex, invalidSkips, missingSkips);
        }

        const u32 dependencyFailures =
            after.unexpectedDependencyFailures - active.statsBefore.unexpectedDependencyFailures;
        if (dependencyFailures != 0 && _reportedDependencyFailureModels.insert(modelIndex).second)
        {
            NC_LOG_ERROR("MODEL_EMBEDDED dependency_failures parent={} count={}", modelIndex,
                         dependencyFailures);
        }

        _placements.emplace(active.source.uniqueID, std::move(active.instances));
    }

    bool ModelPlacementLoader::SetDoodadSet(u32 uniqueID, u16 doodadSet)
    {
        const auto existing = _placements.find(uniqueID);
        return existing != _placements.end() &&
               _embeddedSpawner.SetInstanceSet(existing->second.embedded, doodadSet);
    }

    bool ModelPlacementLoader::SetGeometryGroupEnabled(u32 uniqueID, u32 groupID, bool enabled)
    {
        const auto existing = _placements.find(uniqueID);
        return existing != _placements.end() &&
               _scene->SetGeometryGroupEnabled(existing->second.root, groupID, enabled);
    }

    void ModelPlacementLoader::Clear()
    {
        ZoneScopedN("ModelPlacementLoader::Clear");

        Terrain::Placement pendingPlacement;
        while (_pendingPlacements.try_dequeue(pendingPlacement)) {}

        if (_activePlacement)
        {
            _embeddedSpawner.Destroy(_activePlacement->instances.embedded, 0);
            _scene->DestroyModelInstance(_activePlacement->instances.root, 0);
            _activePlacement.reset();
        }

        for (auto& [uniqueID, placement] : _placements)
        {
            _embeddedSpawner.Destroy(placement.embedded, 0);
            _scene->DestroyModelInstance(placement.root, 0);
        }
        _placements.clear();
        _scene->ReleaseRetiredHistory(0);
        _numQueuedPlacements.store(0, std::memory_order_relaxed);
        _numProcessedPlacements.store(0, std::memory_order_relaxed);
    }

    f32 ModelPlacementLoader::GetLoadingProgress() const
    {
        const u32 queued = _numQueuedPlacements.load(std::memory_order_relaxed);
        if (queued == 0)
            return 1.0f;

        const u32 processed = _numProcessedPlacements.load(std::memory_order_relaxed);
        return static_cast<f32>(processed) / static_cast<f32>(queued);
    }

    ModelPlacementLoaderStats ModelPlacementLoader::GetStats() const
    {
        return {
            .livePlacements = static_cast<u32>(_placements.size()),
            .duplicatePlacements = _duplicatePlacements,
            .sourceResolutionFailures = _sourceResolutionFailures,
            .modelFallbackPlacements = _modelFallbackPlacements,
            .embedded = _embeddedSpawner.GetStats()
        };
    }

    FileFormat::AssetID ModelPlacementLoader::ResolveModelAssetID(u64 sourceReference)
    {
        ZoneScopedN("ModelPlacementLoader::ResolveModelAssetID");

        const auto cached = _sourceAssets.find(sourceReference);
        if (cached != _sourceAssets.end())
            return cached->second;

        const std::string* sourcePath = _pactStorage->GetFilePath(sourceReference);
        if (!sourcePath)
        {
            ++_sourceResolutionFailures;
            _sourceAssets.emplace(sourceReference, FileFormat::INVALID_ASSET_ID);
            return FileFormat::INVALID_ASSET_ID;
        }

        std::filesystem::path modelPath(*sourcePath);
        modelPath.replace_extension(".model");
        std::string normalized = modelPath.generic_string();
        std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
        const FileFormat::AssetID assetID = Util::AssetPath::Hash(normalized);
        _sourceAssets.emplace(sourceReference, assetID);
        return assetID;
    }

    mat4x4 ModelPlacementLoader::MakeWorldTransform(const Terrain::Placement& placement)
    {
        const f32 uniformScale = static_cast<f32>(placement.scale) / 1024.0f;
        const mat4x4 rotation = glm::toMat4(glm::normalize(placement.rotation));
        const mat4x4 scale = glm::scale(mat4x4(1.0f), vec3(uniformScale));
        return glm::translate(mat4x4(1.0f), placement.position) * rotation * scale;
    }
}
