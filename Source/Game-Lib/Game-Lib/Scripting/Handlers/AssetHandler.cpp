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
#include "Game-Lib/Rendering/Scene/RenderScene.h"
#include "Game-Lib/Rendering/Model/ModelRenderSystem.h"
#include "Game-Lib/Rendering/Model/ModelLoader.h"
#include "Game-Lib/Util/AssetPath.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Base/Util/DebugHandler.h>

#include <Scripting/LuaManager.h>
#include <Scripting/Zenith.h>

#include <entt/entt.hpp>
#include <lualib.h>

#include <algorithm>
#include <filesystem>
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
                    path, assetID, static_cast<RenderAssets::ModelHandle::type>(handle), usedFallback, stats.geometry.numModels,
                    stats.geometry.numMeshes, stats.geometry.numMeshlets,
                    stats.materialStorage.numMaterials, stats.materialStorage.numMaterialInstances, stats.textures.resolvedTextures);

        zenith->Push(static_cast<RenderAssets::ModelHandle::type>(handle));
        zenith->Push(usedFallback);
        return 2;
    }

    i32 AssetHandler::GetRenderAssetStats(Zenith* zenith)
    {
        const RenderAssets::RenderAssetResourceStats stats = ServiceLocator::GetGameRenderer()->GetRenderAssetResources()->GetStats();
        const RenderScenes::RenderSceneStats sceneStats = ServiceLocator::GetGameRenderer()->GetWorldRenderScene()->GetStats();
        zenith->CreateTable();
        zenith->AddTableField("models", stats.geometry.numModels);
        zenith->AddTableField("usedBytes", stats.geometry.usedBytes + stats.materialStorage.usedBytes);
        zenith->AddTableField("reservedBytes", stats.geometry.reservedBytes + stats.materialStorage.reservedBytes);
        zenith->AddTableField("growths", stats.geometry.bufferGrowths + stats.materialStorage.bufferGrowths);
        zenith->AddTableField("modelFailures", stats.models.failures);
        zenith->AddTableField("materialFailures", stats.materials.materialFailures + stats.materials.materialInstanceFailures);
        zenith->AddTableField("textureFailures", stats.textures.fallbackTextures);
        zenith->AddTableField("resolvedTextures", stats.textures.resolvedTextures);
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
        const ModelView::DiagnosticWorkStats& diagnosticStats =
            ServiceLocator::GetGameRenderer()->GetModelRenderSystem()->GetDiagnosticStats();
        zenith->AddTableField("diagnosticSelectedInstances", diagnosticStats.selectedInstances);
        zenith->AddTableField("diagnosticOneSidedMeshlets", diagnosticStats.oneSidedMeshlets);
        zenith->AddTableField("diagnosticTwoSidedMeshlets", diagnosticStats.twoSidedMeshlets);
        zenith->AddTableField("diagnosticSkippedSkinnedLODs", diagnosticStats.skippedSkinnedLODs);
        return 1;
    }

    i32 AssetHandler::ShowRenderModel(Zenith* zenith)
    {
        const RenderAssets::ModelHandle model(zenith->CheckVal<u32>(1));
        const vec3 worldBoundsCenter = zenith->CheckVal<vec3>(2);
        const f32 worldBoundsRadius = zenith->CheckVal<f32>(3);
        const RenderScenes::ModelInstanceHandle instance =
            ServiceLocator::GetGameRenderer()->GetModelRenderSystem()->SetDiagnosticModel(
                model, worldBoundsCenter, worldBoundsRadius);
        zenith->Push(static_cast<RenderScenes::ModelInstanceHandle::type>(instance));
        return 1;
    }

    i32 AssetHandler::StressRenderSceneLifecycle(Zenith* zenith)
    {
        constexpr u32 MAX_INSTANCES = 4096;
        constexpr u32 MAX_ITERATIONS = 256;

        const RenderAssets::ModelHandle model(zenith->CheckVal<u32>(1));
        const u32 instanceCount = zenith->CheckVal<u32>(2);
        const u32 iterationCount = zenith->CheckVal<u32>(3);
        if (instanceCount == 0 || instanceCount > MAX_INSTANCES || iterationCount == 0 || iterationCount > MAX_ITERATIONS)
        {
            NC_LOG_ERROR("RENDER_SCENE lifecycle_stress_invalid instances={} iterations={}", instanceCount, iterationCount);
            zenith->Push(false);
            return 1;
        }

        RenderScenes::RenderScene* scene = ServiceLocator::GetGameRenderer()->GetWorldRenderScene();
        std::vector<RenderScenes::ModelInstanceHandle> handles(instanceCount);
        bool succeeded = true;

        for (u32 iteration = 0; iteration < iterationCount && succeeded; ++iteration)
        {
            for (u32 instanceIndex = 0; instanceIndex < instanceCount; ++instanceIndex)
            {
                RenderScenes::ModelInstanceDesc desc;
                desc.model = model;
                desc.worldTransform[3] = vec4(static_cast<f32>(instanceIndex % 64), 0.0f,
                                              static_cast<f32>(instanceIndex / 64), 1.0f);
                handles[instanceIndex] = scene->CreateModelInstance(desc);
                succeeded &= scene->IsPending(handles[instanceIndex]);
            }

            const RenderScenes::SceneClearRequests clearRequests = scene->GetPendingClearRequests();
            succeeded &= clearRequests.instanceSlots.size() >= instanceCount;
            succeeded &= clearRequests.meshletHistoryRanges.size() >= instanceCount;
            scene->AcknowledgeClearsAndPublish();

            for (u32 instanceIndex = 0; instanceIndex < instanceCount; ++instanceIndex)
            {
                const RenderScenes::ModelInstanceHandle handle = handles[instanceIndex];
                succeeded &= scene->IsAlive(handle);

                if ((instanceIndex & 1u) == 0)
                {
                    mat4x4 transform(1.0f);
                    transform[3] = vec4(static_cast<f32>(instanceIndex % 64), 1.0f,
                                        static_cast<f32>(instanceIndex / 64), 1.0f);
                    succeeded &= scene->SetModelTransform(handle, transform, true);
                }
                else
                {
                    succeeded &= scene->SetModelVisible(handle, false);
                    succeeded &= scene->SetModelVisible(handle, true);
                }
            }

            scene->AcknowledgeClearsAndPublish();
            for (const RenderScenes::ModelInstanceHandle handle : handles)
                succeeded &= scene->DestroyModelInstance(handle, iteration + 1u);

            scene->ReleaseRetiredHistory(iteration);
            succeeded &= scene->GetStats().meshletHistory.retiredWords != 0;
            scene->ReleaseRetiredHistory(iteration + 1u);
        }

        const RenderScenes::RenderSceneStats stats = scene->GetStats();
        succeeded &= stats.instances.liveInstances == 0;
        succeeded &= stats.instances.pendingInstances == 0;
        succeeded &= stats.meshletHistory.liveWords == 0;
        succeeded &= stats.meshletHistory.retiredWords == 0;
        succeeded &= stats.meshletHistory.addressSpaceWords == 0;

        NC_LOG_INFO("RENDER_SCENE lifecycle_stress_complete success={} instances={} iterations={} slots={} staleRejects={} historyHighWater={}",
                    succeeded, instanceCount, iterationCount, stats.instances.slotCapacity,
                    stats.instances.staleHandleRejects, stats.meshletHistory.highWaterWords);
        zenith->Push(succeeded);
        return 1;
    }
}
