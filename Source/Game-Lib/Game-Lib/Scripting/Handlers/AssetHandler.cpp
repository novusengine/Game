#include "AssetHandler.h"
#include "Game-Lib/Application/EnttRegistries.h"
#include "Game-Lib/ECS/Components/AABB.h"
#include "Game-Lib/ECS/Components/Model.h"
#include "Game-Lib/ECS/Components/Name.h"
#include "Game-Lib/ECS/Singletons/ActiveCamera.h"
#include "Game-Lib/ECS/Singletons/EditorSelection.h"
#include "Game-Lib/ECS/Util/Transforms.h"
#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Rendering/Asset/RenderAssetResources.h"
#include "Game-Lib/Rendering/Light/LightRenderer.h"
#include "Game-Lib/Rendering/Material/MaterialStorage.h"
#include "Game-Lib/Rendering/Scene/RenderScene.h"
#include "Game-Lib/Rendering/Model/ModelRenderSystem.h"
#include "Game-Lib/Rendering/Model/ModelPlacementLoader.h"
#include "Game-Lib/Rendering/Model/ModelLoader.h"
#include "Game-Lib/Rendering/PixelQuery.h"
#include "Game-Lib/Rendering/Shadow/ModelShadowWork.h"
#include "Game-Lib/Rendering/Shadow/ShadowRenderer.h"
#include "Game-Lib/Util/AssetPath.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Base/Util/DebugHandler.h>

#include <Scripting/LuaManager.h>
#include <Scripting/Zenith.h>

#include <entt/entt.hpp>
#include <lualib.h>

#include <chrono>

#include <algorithm>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace Scripting::Asset
{
    void AssetHandler::Register(Zenith* zenith)
    {
        LuaManager* luaManager = ServiceLocator::GetLuaManager();
        const bool inDeveloperMode = luaManager && luaManager->IsDeveloperMode();
        const Scripting::LuaMethodFlags excludeFlags = inDeveloperMode
            ? Scripting::LuaMethodFlags::None
            : Scripting::LuaMethodFlags::DeveloperOnly;

        LuaMethodTable::Set(zenith, assetGlobalMethods, "Asset", excludeFlags);
    }

    static fs::path GetDataRoot()
    {
        return fs::absolute("Data");
    }

    // Turns an absolute path under Data/ into a forward-slash path relative to Data/.
    static std::string ToDataRelative(const fs::path& path, const fs::path& dataRoot)
    {
        std::error_code ec;
        std::string relative = fs::relative(path, dataRoot, ec).string();
        if (ec)
            relative = path.filename().string();
        std::replace(relative.begin(), relative.end(), '\\', '/');
        return relative;
    }

    // Counts the immediate files and subfolders of `folder` in a single pass.
    static void CountFolderContents(const fs::path& folder, u32& fileCount, u32& folderCount)
    {
        fileCount = 0;
        folderCount = 0;
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(folder, ec))
        {
            if (entry.is_directory())
                ++folderCount;
            else
                ++fileCount;
        }
    }

    i32 AssetHandler::ListDir(Zenith* zenith)
    {
        const char* relativeRaw = zenith->IsString(1) ? zenith->Get<const char*>(1) : "";
        std::string relative = relativeRaw ? relativeRaw : "";

        fs::path dataRoot = GetDataRoot();
        fs::path target = relative.empty() ? dataRoot : (dataRoot / relative);

        zenith->CreateTable();

        // folders
        zenith->CreateTable();
        i32 folderIndex = 0;

        // files
        std::vector<fs::path> files;

        std::error_code ec;
        if (fs::is_directory(target, ec))
        {
            for (const auto& entry : fs::directory_iterator(target, ec))
            {
                const fs::path& entryPath = entry.path();
                if (entry.is_directory())
                {
                    u32 fileCount = 0;
                    u32 folderCount = 0;
                    CountFolderContents(entryPath, fileCount, folderCount);

                    zenith->CreateTable();
                    zenith->AddTableField("name", entryPath.filename().string().c_str());
                    zenith->AddTableField("path", ToDataRelative(entryPath, dataRoot).c_str());
                    zenith->AddTableField("fileCount", fileCount);
                    zenith->AddTableField("folderCount", folderCount);
                    zenith->SetTableKey(++folderIndex);
                }
                else
                {
                    files.push_back(entryPath);
                }
            }
        }
        zenith->SetTableKey("folders");

        zenith->CreateTable();
        for (size_t i = 0; i < files.size(); ++i)
        {
            const fs::path& file = files[i];
            std::string ext = file.extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(::tolower(c)); });

            zenith->CreateTable();
            zenith->AddTableField("name", file.filename().string().c_str());
            zenith->AddTableField("path", ToDataRelative(file, dataRoot).c_str());
            zenith->AddTableField("ext", ext.c_str());
            zenith->SetTableKey(static_cast<i32>(i + 1));
        }
        zenith->SetTableKey("files");

        return 1;
    }

    entt::entity AssetHandler::CreateModelAtPosition(const std::string& dataRelativePath, const vec3& position)
    {
        EnttRegistries* registries = ServiceLocator::GetEnttRegistries();
        entt::registry& registry = *registries->gameRegistry;

        // The model loader expects a path relative to Data/ComplexModel with forward slashes.
        fs::path dataRoot = GetDataRoot();
        std::error_code ec;
        std::string modelPath = fs::relative(dataRoot / dataRelativePath, dataRoot / "ComplexModel", ec).string();
        if (ec)
            modelPath = dataRelativePath;
        std::replace(modelPath.begin(), modelPath.end(), '\\', '/');

        entt::entity entity = registry.create();
        registry.emplace<ECS::Components::Name>(entity);
        auto& model = registry.emplace<ECS::Components::Model>(entity);
        registry.emplace<ECS::Components::AABB>(entity);
        registry.emplace<ECS::Components::Transform>(entity);

        ECS::TransformSystem::Get(registry).SetLocalTransform(entity, position, quat(1.0f, 0.0f, 0.0f, 0.0f), vec3(1.0f, 1.0f, 1.0f));

        u64 modelPathHash = ServiceLocator::GetGameRenderer()->GetModelLoader()->GetModelHashFromModelPath(modelPath);
        ServiceLocator::GetGameRenderer()->GetModelLoader()->LoadModelForEntity(entity, model, modelPathHash);

        return entity;
    }

    i32 AssetHandler::SpawnModel(Zenith* zenith)
    {
        const char* relativeRaw = zenith->CheckVal<const char*>(1);
        if (!relativeRaw)
        {
            zenith->Push();
            return 1;
        }

        EnttRegistries* registries = ServiceLocator::GetEnttRegistries();
        entt::registry& registry = *registries->gameRegistry;
        entt::registry::context& ctx = registry.ctx();

        auto& activeCamera = ctx.get<ECS::Singletons::ActiveCamera>();
        if (activeCamera.entity == entt::null)
        {
            zenith->Push();
            return 1;
        }

        auto& cameraTransform = registry.get<ECS::Components::Transform>(activeCamera.entity);
        entt::entity entity = CreateModelAtPosition(relativeRaw, cameraTransform.GetWorldPosition());

        zenith->Push(entt::to_integral(entity));
        return 1;
    }

    i32 AssetHandler::BeginDragSpawn(Zenith* zenith)
    {
        const char* relativeRaw = zenith->CheckVal<const char*>(1);
        if (!relativeRaw)
            return 0;

        EnttRegistries* registries = ServiceLocator::GetEnttRegistries();
        entt::registry::context& ctx = registries->gameRegistry->ctx();
        auto& selection = ctx.get<ECS::Singletons::EditorSelection>();
        selection.dragSpawnRequested = true;
        selection.dragSpawnModelPath = relativeRaw;

        return 0;
    }

    i32 AssetHandler::LoadRenderModel(Zenith* zenith)
    {
        const char* pathRaw = zenith->CheckVal<const char*>(1);
        if (!pathRaw)
            return 0;

        std::string path = pathRaw;
        std::replace(path.begin(), path.end(), '\\', '/');
        std::transform(path.begin(), path.end(), path.begin(), [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });

        RenderAssets::RenderAssetResources* resources = ServiceLocator::GetGameRenderer()->GetRenderAssetResources();
        const FileFormat::AssetID assetID = Util::AssetPath::Hash(path);
        const RenderAssets::ModelHandle handle = resources->LoadModel(assetID);
        const bool usedFallback = handle == resources->GetFallbackModel();
        const RenderAssets::RenderAssetResourceStats stats = resources->GetStats();
        NC_LOG_INFO("MODEL_ASSET debug_load path={} asset={} handle={} fallback={} models={} meshes={} meshlets={} materials={} instances={} textures={}",
                    path, assetID, static_cast<RenderAssets::ModelHandle::type>(handle), usedFallback,
                    stats.modelGeometry.numModels, stats.modelGeometry.numMeshes, stats.modelGeometry.numMeshlets,
                    stats.materialStorage.numMaterials, stats.materialStorage.numMaterialInstances, stats.textures.resolvedTextures);

        zenith->Push(static_cast<RenderAssets::ModelHandle::type>(handle));
        zenith->Push(usedFallback);
        return 2;
    }

    i32 AssetHandler::GetRenderAssetStats(Zenith* zenith)
    {
        const RenderAssets::RenderAssetResourceStats stats = ServiceLocator::GetGameRenderer()->GetRenderAssetResources()->GetStats();
        const RenderScenes::RenderSceneStats sceneStats = ServiceLocator::GetGameRenderer()->GetWorldRenderScene()->GetStats();
        const ModelRendering::ModelPlacementLoaderStats placementStats =
            ServiceLocator::GetGameRenderer()->GetModelPlacementLoader()->GetStats();
        zenith->CreateTable();
        zenith->AddTableField("models", stats.modelGeometry.numModels);
        zenith->AddTableField("residentModels", stats.models.residentModels);
        zenith->AddTableField("modelReferences", stats.models.references);
        zenith->AddTableField("modelCacheHits", stats.models.cacheHits);
        zenith->AddTableField("modelPendingLoads", stats.models.pendingLoads);
        zenith->AddTableField("modelActiveJobs", stats.models.activeJobs);
        zenith->AddTableField("modelPeakActiveJobs", stats.models.peakActiveJobs);
        zenith->AddTableField("modelPeakPendingLoads", stats.models.peakPendingLoads);
        zenith->AddTableField("modelCoalescedRequests", stats.models.coalescedRequests);
        zenith->AddTableField("modelCompletedReads", stats.models.completedReads);
        zenith->AddTableField("modelCommittedLoads", stats.models.committedLoads);
        zenith->AddTableField("modelCancelledLoads", stats.models.cancelledLoads);
        zenith->AddTableField("modelDecodedBytes", stats.models.decodedBytes);
        zenith->AddTableField("modelPeakDecodedBytes", stats.models.peakDecodedBytes);
        zenith->AddTableField("modelTotalCommitTimeUS", stats.models.totalCommitTimeUS);
        zenith->AddTableField("modelMaxCommitTimeUS", stats.models.maxCommitTimeUS);
        zenith->AddTableField("usedBytes", stats.modelGeometry.usedBytes + stats.materialStorage.usedBytes);
        zenith->AddTableField("reservedBytes", stats.modelGeometry.reservedBytes + stats.materialStorage.reservedBytes);
        zenith->AddTableField("growths", stats.modelGeometry.bufferGrowths + stats.materialStorage.bufferGrowths);
        zenith->AddTableField("modelFailures", stats.models.failures);
        zenith->AddTableField("materialFailures", stats.materials.materialFailures + stats.materials.materialInstanceFailures);
        zenith->AddTableField("textureFailures", stats.textures.fallbackTextures);
        zenith->AddTableField("resolvedTextures", stats.textures.resolvedTextures);
        zenith->AddTableField("decalTileOverflow", ServiceLocator::GetGameRenderer()->GetLightRenderer()->GetDecalTileOverflowCount());
        zenith->AddTableField("sceneInstances", sceneStats.instances.liveInstances);
        zenith->AddTableField("scenePendingInstances", sceneStats.instances.pendingInstances);
        zenith->AddTableField("sceneInstanceSlots", sceneStats.instances.slotCapacity);
        zenith->AddTableField("sceneStaleHandleRejects", sceneStats.instances.staleHandleRejects);
        zenith->AddTableField("sceneMaterialTables", sceneStats.materialTables.sharedTables + sceneStats.materialTables.privateTables);
        zenith->AddTableField("sceneGeometryGroupMaskWords", sceneStats.geometryGroupMasks.liveWords);
        zenith->AddTableField("sceneHistoryWords", sceneStats.meshletHistory.liveWords);
        zenith->AddTableField("sceneHistoryAddressWords", sceneStats.meshletHistory.addressSpaceWords);
        zenith->AddTableField("sceneHistoryRetiredWords", sceneStats.meshletHistory.retiredWords);
        zenith->AddTableField("sceneHistoryClearRanges", sceneStats.meshletHistory.pendingClearRanges);
        const ShadowRendering::SceneShadowStats sceneShadowStats = ServiceLocator::GetGameRenderer()->GetWorldRenderScene()->GetShadowStats();
        zenith->AddTableField("shadowDynamicCasters", sceneShadowStats.dynamicCasters);
        const ShadowRendering::ModelShadowStats& shadowStats = ServiceLocator::GetGameRenderer()->GetShadowRenderer()->GetModelShadowStats();
        u32 staticShadowMeshlets = 0;
        u32 dynamicShadowMeshlets = 0;
        for (u32 clipmap = 0; clipmap < ShadowRendering::MODEL_SHADOW_MAX_CLIPMAPS; ++clipmap)
        {
            const u32 base = clipmap * ShadowRendering::MODEL_SHADOW_QUEUE_COUNT;
            for (u32 queue = 0; queue < ShadowRendering::MODEL_SHADOW_RASTER_CLASS_COUNT; ++queue)
            {
                staticShadowMeshlets += shadowStats.survivingMeshlets[base + queue];
                dynamicShadowMeshlets += shadowStats.survivingMeshlets[base + ShadowRendering::MODEL_SHADOW_RASTER_CLASS_COUNT + queue];
            }
        }
        zenith->AddTableField("shadowStaticMeshlets", staticShadowMeshlets);
        zenith->AddTableField("shadowDynamicMeshlets", dynamicShadowMeshlets);
        zenith->AddTableField("shadowTestedMeshlets", shadowStats.testedMeshlets);
        zenith->AddTableField("shadowPageRejectedMeshlets", shadowStats.rejectedPageMeshlets);
        zenith->AddTableField("shadowGeometryGroupRejectedMeshlets", shadowStats.rejectedGeometryGroupMeshlets);
        zenith->AddTableField("shadowBoundsRejectedMeshlets", shadowStats.rejectedBoundsMeshlets);
        zenith->AddTableField("shadowQueueOverflows", shadowStats.queueOverflows + shadowStats.recordOverflows + shadowStats.chunkQueueOverflows);
        zenith->AddTableField("mapPlacements", placementStats.livePlacements);
        zenith->AddTableField("mapPlacementDuplicates", placementStats.duplicatePlacements);
        zenith->AddTableField("mapPlacementResolutionFailures", placementStats.sourceResolutionFailures);
        zenith->AddTableField("mapPlacementFallbacks", placementStats.modelFallbackPlacements);
        zenith->AddTableField("mapPlacementSourceLookups", placementStats.sourceAssetLookups);
        zenith->AddTableField("mapPlacementSourceAssets", placementStats.sourceAssetCacheEntries);
        zenith->AddTableField("embeddedInstances", placementStats.embedded.spawnedInstances);
        zenith->AddTableField("embeddedInvalidReferenceSkips", placementStats.embedded.invalidReferenceSkips);
        zenith->AddTableField("embeddedMissingGeometrySkips", placementStats.embedded.missingGeometrySkips);
        zenith->AddTableField("embeddedDependencyFailures", placementStats.embedded.unexpectedDependencyFailures);
        const ModelView::WorkStats& modelStats =
            ServiceLocator::GetGameRenderer()->GetModelRenderSystem()->GetMainViewStats();
        zenith->AddTableField("modelSelectedInstances", modelStats.selectedInstances);
        zenith->AddTableField("modelOneSidedMeshlets", modelStats.oneSidedMeshlets);
        zenith->AddTableField("modelTwoSidedMeshlets", modelStats.twoSidedMeshlets);
        zenith->AddTableField("modelSkippedSkinnedLODs", modelStats.skippedSkinnedLODs);
        zenith->AddTableField("modelTestedInstances", modelStats.testedInstances);
        zenith->AddTableField("modelRejectedInstances", modelStats.rejectedInstances);
        zenith->AddTableField("modelExpandedChunks", modelStats.expandedChunks);
        zenith->AddTableField("modelTestedChunks", modelStats.testedChunks);
        zenith->AddTableField("modelTestedMeshlets", modelStats.testedMeshlets);
        zenith->AddTableField("modelFrustumRejectedMeshlets", modelStats.rejectedFrustumMeshlets);
        zenith->AddTableField("modelConeRejectedMeshlets", modelStats.rejectedConeMeshlets);
        zenith->AddTableField("modelTestedTriangles", modelStats.testedTriangles);
        zenith->AddTableField("modelCommittedTriangles", modelStats.committedTriangles);
        zenith->AddTableField("modelQueueOverflows", modelStats.queueOverflows);
        zenith->AddTableField("modelChunkQueueOverflows", modelStats.chunkQueueOverflows);
        zenith->AddTableField("modelVisibilityRecordOverflows", modelStats.visibilityRecordOverflows);
        zenith->AddTableField("modelSurvivors", modelStats.survivorCount);
        zenith->AddTableField("modelPhase1ReplayedMeshlets", modelStats.phase1ReplayedMeshlets);
        zenith->AddTableField("modelPhase2AddedMeshlets", modelStats.phase2AddedMeshlets);
        zenith->AddTableField("modelOcclusionRejectedMeshlets", modelStats.rejectedOcclusionMeshlets);
        for (u32 lod = 0; lod < 8; ++lod)
        {
            const std::string field = "modelLOD" + std::to_string(lod);
            zenith->AddTableField(field.c_str(), modelStats.lodSelections[lod]);
        }
        return 1;
    }
}
