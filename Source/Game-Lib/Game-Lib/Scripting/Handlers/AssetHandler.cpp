#include "AssetHandler.h"
#include "Game-Lib/Application/EnttRegistries.h"
#include "Game-Lib/ECS/Components/AABB.h"
#include "Game-Lib/ECS/Components/Model.h"
#include "Game-Lib/ECS/Components/Name.h"
#include "Game-Lib/ECS/Singletons/ActiveCamera.h"
#include "Game-Lib/ECS/Singletons/EditorSelection.h"
#include "Game-Lib/ECS/Util/Transforms.h"
#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Rendering/Animation/Deformation/ModelDeformationSystem.h"
#include "Game-Lib/Rendering/Asset/RenderAssetResources.h"
#include "Game-Lib/Rendering/Asset/AssetDiagnostic.h"
#include "Game-Lib/Rendering/Light/LightRenderer.h"
#include "Game-Lib/Rendering/Material/MaterialStorage.h"
#include "Game-Lib/Rendering/Scene/RenderScene.h"
#include "Game-Lib/Rendering/Model/ModelRenderSystem.h"
#include "Game-Lib/Rendering/Model/ModelPlacementLoader.h"
#include "Game-Lib/Rendering/Model/ModelLoader.h"
#include "Game-Lib/Rendering/Model/Scene/ModelSceneBridge.h"
#include "Game-Lib/Rendering/PixelQuery.h"
#include "Game-Lib/Rendering/Shadow/ModelShadowWork.h"
#include "Game-Lib/Rendering/Shadow/ShadowRenderer.h"
#include "Game-Lib/Util/AssetPath.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Base/Util/DebugHandler.h>

#include <Scripting/LuaManager.h>
#include <Scripting/Zenith.h>

#include <Filesystem/PactStorage.h>

#include <entt/entt.hpp>
#include <lualib.h>

#include <chrono>

#include <algorithm>
#include <filesystem>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

namespace Scripting::Asset
{
    static std::unordered_map<u32, Animation::DirectPlaybackHandle> s_directPlaybackByEntity;

    static bool StartsWithCaseInsensitive(std::string_view value, std::string_view prefix)
    {
        if (value.size() < prefix.size())
            return false;

        return std::equal(prefix.begin(), prefix.end(), value.begin(), [](unsigned char lhs, unsigned char rhs) { return std::tolower(lhs) == std::tolower(rhs); });
    }

    static void AddUniqueCandidate(std::vector<std::string>& candidates, std::string candidate)
    {
        if (!candidate.empty() && std::find(candidates.begin(), candidates.end(), candidate) == candidates.end())
            candidates.push_back(std::move(candidate));
    }

    static bool ResolveModelAssetPath(const std::string& dataRelativePath, PACT::PactStorage* pactStorage, std::string& resolvedPath, FileFormat::AssetID& assetID)
    {
        std::string normalizedPath = dataRelativePath;
        std::replace(normalizedPath.begin(), normalizedPath.end(), '\\', '/');
        while (normalizedPath.starts_with("./"))
            normalizedPath.erase(0, 2);

        std::vector<std::string> candidates;
        constexpr std::string_view overlayPrefixes[] = { "pact/data/staging/", "pact/data/custom/", "data/pact/data/staging/", "data/pact/data/custom/" };
        for (std::string_view prefix : overlayPrefixes)
        {
            if (StartsWithCaseInsensitive(normalizedPath, prefix))
                AddUniqueCandidate(candidates, normalizedPath.substr(prefix.size()));
        }
        AddUniqueCandidate(candidates, normalizedPath);

        const size_t casePreservingCandidateCount = candidates.size();
        for (size_t i = 0; i < casePreservingCandidateCount; ++i)
        {
            std::string lowercaseCandidate = candidates[i];
            std::transform(lowercaseCandidate.begin(), lowercaseCandidate.end(), lowercaseCandidate.begin(), [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
            AddUniqueCandidate(candidates, std::move(lowercaseCandidate));
        }

        for (const std::string& candidate : candidates)
        {
            const FileFormat::AssetID candidateID = Util::AssetPath::Hash(candidate);
            if (pactStorage->FileExists(candidateID))
            {
                resolvedPath = candidate;
                assetID = candidateID;
                return true;
            }
        }

        return false;
    }

    void AssetHandler::Register(Zenith* zenith)
    {
        LuaManager* luaManager = ServiceLocator::GetLuaManager();
        const bool inDeveloperMode = luaManager && luaManager->IsDeveloperMode();
        const Scripting::LuaMethodFlags excludeFlags = inDeveloperMode
            ? Scripting::LuaMethodFlags::None
            : Scripting::LuaMethodFlags::DeveloperOnly;

        LuaMethodTable::Set(zenith, assetGlobalMethods, "Asset", excludeFlags);
    }

    void AssetHandler::Clear(Zenith*)
    {
        GameRenderer* gameRenderer = ServiceLocator::GetGameRenderer();
        Animation::ModelDeformationSystem* deformation = gameRenderer ? gameRenderer->GetModelRenderSystem()->GetDeformationSystem() : nullptr;
        if (deformation)
            for (const auto& [entity, playback] : s_directPlaybackByEntity)
                deformation->DestroyDirectPlayback(playback);
        s_directPlaybackByEntity.clear();
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
        PACT::PactStorage* pactStorage = ServiceLocator::GetPactStorage();
        std::string modelPath;
        FileFormat::AssetID modelPathHash = FileFormat::INVALID_ASSET_ID;
        if (!pactStorage || !ResolveModelAssetPath(dataRelativePath, pactStorage, modelPath, modelPathHash))
        {
            NC_LOG_ERROR("Asset Browser : Failed to spawn model '{}': no mounted PACT asset matches the browser path", dataRelativePath);
            return entt::null;
        }

        EnttRegistries* registries = ServiceLocator::GetEnttRegistries();
        entt::registry& registry = *registries->gameRegistry;

        entt::entity entity = registry.create();
        registry.emplace<ECS::Components::Name>(entity);
        auto& model = registry.emplace<ECS::Components::Model>(entity);
        registry.emplace<ECS::Components::AABB>(entity);
        registry.emplace<ECS::Components::Transform>(entity);

        ECS::TransformSystem::Get(registry).SetLocalTransform(entity, position, quat(1.0f, 0.0f, 0.0f, 0.0f), vec3(1.0f, 1.0f, 1.0f));

        if (!ServiceLocator::GetGameRenderer()->GetModelLoader()->LoadModelForEntity(entity, model, modelPathHash))
        {
            registry.destroy(entity);
            NC_LOG_ERROR("Asset Browser : Failed to queue model '{}' (resolved PACT path '{}', asset {})", dataRelativePath, modelPath, modelPathHash);
            return entt::null;
        }

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
            NC_LOG_ERROR("Asset Browser : Failed to spawn model '{}': there is no active camera", relativeRaw);
            zenith->Push();
            return 1;
        }

        auto& cameraTransform = registry.get<ECS::Components::Transform>(activeCamera.entity);
        entt::entity entity = CreateModelAtPosition(relativeRaw, cameraTransform.GetWorldPosition());

        zenith->Push(entt::to_integral(entity));
        return 1;
    }

    i32 AssetHandler::SpawnModelAtWorldOrigin(Zenith* zenith)
    {
        const char* relativeRaw = zenith->CheckVal<const char*>(1);
        if (!relativeRaw)
        {
            zenith->Push();
            return 1;
        }

        const entt::entity entity = CreateModelAtPosition(relativeRaw, vec3(0.0f));
        zenith->Push(entt::to_integral(entity));
        return 1;
    }

    i32 AssetHandler::BindRestPose(Zenith* zenith)
    {
        const u32 entityValue = zenith->CheckVal<u32>(1);
        const char* skeletonPathRaw = zenith->CheckVal<const char*>(2);
        if (!skeletonPathRaw)
        {
            zenith->Push(false);
            return 1;
        }
        GameRenderer* gameRenderer = ServiceLocator::GetGameRenderer();
        ModelScene::ModelSceneBridge* bridge = gameRenderer->GetModelSceneBridge();
        const entt::entity entity = static_cast<entt::entity>(entityValue);
        RenderScenes::RenderScene* scene = bridge->GetScene(entity);
        Animation::ModelDeformationSystem* deformation = scene ? scene->GetModelDeformationSystem() : nullptr;
        if (!deformation)
        {
            zenith->Push(false);
            return 1;
        }

        std::string skeletonPath;
        FileFormat::AssetID skeletonAssetID = FileFormat::INVALID_ASSET_ID;
        PACT::PactStorage* pactStorage = ServiceLocator::GetPactStorage();
        if (!pactStorage || !ResolveModelAssetPath(skeletonPathRaw, pactStorage, skeletonPath, skeletonAssetID))
        {
            zenith->Push(false);
            return 1;
        }
        AnimationLoading::AnimationAssetRegistry& registry = gameRenderer->GetRenderAssetResources()->GetAnimationRegistry();
        const AnimationLoading::SkeletonHandle skeleton = registry.LoadSkeleton(skeletonAssetID);
        AssetLoading::Diagnostic diagnostic;
        const AnimationLoading::SkeletonAssetView* view = registry.Get(skeleton, &diagnostic);
        if (!view)
        {
            registry.Release(skeleton);
            NC_LOG_ERROR("ANIMATION_PREVIEW bind_rest_pose_failed entity={} skeleton={} diagnostic={}", entityValue, skeletonPath, AssetLoading::Describe(diagnostic));
            zenith->Push(false);
            return 1;
        }
        const Animation::PoseSourceHandle poseSource = deformation->CreateRestPose(skeleton, *view);
        if (!poseSource || !bridge->SetPoseSource(entity, poseSource))
        {
            if (poseSource)
                deformation->DestroyPoseSource(poseSource);
            registry.Release(skeleton);
            zenith->Push(false);
            return 1;
        }
        NC_LOG_INFO("ANIMATION_PREVIEW rest_pose_bound entity={} skeleton={} joints={} pose_source={}", entityValue, skeletonPath, view->joints.size(), poseSource.GetRawValue());
        zenith->Push(true);
        return 1;
    }

    i32 AssetHandler::BindAnimation(Zenith* zenith)
    {
        const u32 entityValue = zenith->CheckVal<u32>(1);
        const char* skeletonPathRaw = zenith->CheckVal<const char*>(2);
        const char* clipPathRaw = zenith->CheckVal<const char*>(3);
        GameRenderer* gameRenderer = ServiceLocator::GetGameRenderer();
        ModelScene::ModelSceneBridge* bridge = gameRenderer ? gameRenderer->GetModelSceneBridge() : nullptr;
        const entt::entity entity = static_cast<entt::entity>(entityValue);
        RenderScenes::RenderScene* scene = bridge ? bridge->GetScene(entity) : nullptr;
        Animation::ModelDeformationSystem* deformation = scene ? scene->GetModelDeformationSystem() : nullptr;
        if (!skeletonPathRaw || !clipPathRaw || !deformation)
        {
            zenith->Push(false);
            return 1;
        }

        PACT::PactStorage* pactStorage = ServiceLocator::GetPactStorage();
        std::string skeletonPath;
        std::string clipPath;
        FileFormat::AssetID skeletonAssetID = FileFormat::INVALID_ASSET_ID;
        FileFormat::AssetID clipAssetID = FileFormat::INVALID_ASSET_ID;
        if (!pactStorage || !ResolveModelAssetPath(skeletonPathRaw, pactStorage, skeletonPath, skeletonAssetID) || !ResolveModelAssetPath(clipPathRaw, pactStorage, clipPath, clipAssetID))
        {
            zenith->Push(false);
            return 1;
        }

        AnimationLoading::AnimationAssetRegistry& assets = gameRenderer->GetRenderAssetResources()->GetAnimationRegistry();
        const AnimationLoading::SkeletonHandle skeleton = assets.LoadSkeleton(skeletonAssetID);
        const AnimationLoading::AnimationClipHandle clip = assets.LoadAnimationClip(clipAssetID);
        AssetLoading::Diagnostic skeletonDiagnostic;
        AssetLoading::Diagnostic clipDiagnostic;
        const AnimationLoading::SkeletonAssetView* skeletonView = assets.Get(skeleton, &skeletonDiagnostic);
        const AnimationLoading::AnimationClipAssetView* clipView = assets.Get(clip, &clipDiagnostic);
        if (!skeletonView || !clipView)
        {
            assets.Release(clip);
            assets.Release(skeleton);
            NC_LOG_ERROR("ANIMATION_PREVIEW bind_failed entity={} skeleton={} skeleton_diagnostic={} clip={} clip_diagnostic={}", entityValue, skeletonPath, AssetLoading::Describe(skeletonDiagnostic), clipPath, AssetLoading::Describe(clipDiagnostic));
            zenith->Push(false);
            return 1;
        }

        const Animation::DirectPlaybackHandle playback = deformation->CreateDirectPlayback(skeleton, clip, *skeletonView, *clipView);
        const Animation::PoseSourceHandle poseSource = deformation->GetPlaybackPoseSource(playback);
        if (!playback || !poseSource || !bridge->SetPoseSource(entity, poseSource))
        {
            if (playback)
                deformation->DestroyDirectPlayback(playback);
            else
            {
                assets.Release(clip);
                assets.Release(skeleton);
            }
            zenith->Push(false);
            return 1;
        }

        const auto existing = s_directPlaybackByEntity.find(entityValue);
        if (existing != s_directPlaybackByEntity.end())
            deformation->DestroyDirectPlayback(existing->second);
        s_directPlaybackByEntity[entityValue] = playback;
        bridge->SetShadowDynamic(entity, true);
        NC_LOG_INFO("ANIMATION_PREVIEW clip_bound entity={} skeleton={} clip={} joints={} tracks={} playback={}", entityValue, skeletonPath, clipPath, skeletonView->joints.size(), clipView->tracks.size(), playback.GetRawValue());
        zenith->Push(true);
        return 1;
    }

    static Animation::ModelDeformationSystem* GetPlayback(u32 entityValue, Animation::DirectPlaybackHandle& playback)
    {
        const auto found = s_directPlaybackByEntity.find(entityValue);
        GameRenderer* gameRenderer = ServiceLocator::GetGameRenderer();
        if (found == s_directPlaybackByEntity.end() || !gameRenderer)
            return nullptr;
        playback = found->second;
        return gameRenderer->GetModelRenderSystem()->GetDeformationSystem();
    }

    i32 AssetHandler::AnimationPlay(Zenith* zenith)
    {
        Animation::DirectPlaybackHandle playback;
        Animation::ModelDeformationSystem* deformation = GetPlayback(zenith->CheckVal<u32>(1), playback);
        zenith->Push(deformation && deformation->Play(playback));
        return 1;
    }

    i32 AssetHandler::AnimationPause(Zenith* zenith)
    {
        Animation::DirectPlaybackHandle playback;
        Animation::ModelDeformationSystem* deformation = GetPlayback(zenith->CheckVal<u32>(1), playback);
        zenith->Push(deformation && deformation->Pause(playback));
        return 1;
    }

    i32 AssetHandler::AnimationStop(Zenith* zenith)
    {
        Animation::DirectPlaybackHandle playback;
        Animation::ModelDeformationSystem* deformation = GetPlayback(zenith->CheckVal<u32>(1), playback);
        zenith->Push(deformation && deformation->Stop(playback));
        return 1;
    }

    i32 AssetHandler::AnimationSeek(Zenith* zenith)
    {
        Animation::DirectPlaybackHandle playback;
        Animation::ModelDeformationSystem* deformation = GetPlayback(zenith->CheckVal<u32>(1), playback);
        zenith->Push(deformation && deformation->Seek(playback, zenith->CheckVal<f64>(2)));
        return 1;
    }

    i32 AssetHandler::AnimationSetLooping(Zenith* zenith)
    {
        Animation::DirectPlaybackHandle playback;
        Animation::ModelDeformationSystem* deformation = GetPlayback(zenith->CheckVal<u32>(1), playback);
        zenith->Push(deformation && deformation->SetLooping(playback, zenith->CheckVal<bool>(2)));
        return 1;
    }

    i32 AssetHandler::AnimationSetPlaybackRate(Zenith* zenith)
    {
        Animation::DirectPlaybackHandle playback;
        Animation::ModelDeformationSystem* deformation = GetPlayback(zenith->CheckVal<u32>(1), playback);
        zenith->Push(deformation && deformation->SetPlaybackRate(playback, zenith->CheckVal<f32>(2)));
        return 1;
    }

    i32 AssetHandler::GetAnimationState(Zenith* zenith)
    {
        Animation::DirectPlaybackHandle playback;
        Animation::ModelDeformationSystem* deformation = GetPlayback(zenith->CheckVal<u32>(1), playback);
        Animation::DirectPlaybackState state;
        if (!deformation || !deformation->GetPlaybackState(playback, state))
        {
            zenith->Push();
            return 1;
        }
        zenith->CreateTable();
        zenith->AddTableField("time", state.timeSeconds);
        zenith->AddTableField("duration", state.durationSeconds);
        zenith->AddTableField("playbackRate", state.playbackRate);
        zenith->AddTableField("playing", state.playing);
        zenith->AddTableField("looping", state.looping);
        return 1;
    }

    i32 AssetHandler::DestroyAnimatedModel(Zenith* zenith)
    {
        const u32 entityValue = zenith->CheckVal<u32>(1);
        Animation::DirectPlaybackHandle playback;
        Animation::ModelDeformationSystem* deformation = GetPlayback(entityValue, playback);
        bool destroyed = false;
        if (deformation)
        {
            if (GameRenderer* gameRenderer = ServiceLocator::GetGameRenderer())
                gameRenderer->GetModelSceneBridge()->SetPoseSource(static_cast<entt::entity>(entityValue), {});
            destroyed = deformation->DestroyDirectPlayback(playback);
            s_directPlaybackByEntity.erase(entityValue);
        }
        EnttRegistries* registries = ServiceLocator::GetEnttRegistries();
        const entt::entity entity = static_cast<entt::entity>(entityValue);
        if (registries && registries->gameRegistry->valid(entity))
        {
            registries->gameRegistry->destroy(entity);
            destroyed = true;
        }
        zenith->Push(destroyed);
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
        const Animation::ModelDeformationSystem* deformation = ServiceLocator::GetGameRenderer()->GetModelRenderSystem()->GetDeformationSystem();
        const Animation::ModelDeformationStats& deformationStats = deformation->GetStats();
        const Animation::DirectPlaybackStats& playbackStats = deformation->GetPlaybackStats();
        zenith->AddTableField("animationPoseSources", deformationStats.poseSources);
        zenith->AddTableField("animationPoseRequests", deformationStats.requests);
        zenith->AddTableField("animationDeformationDispatches", deformationStats.dispatches);
        zenith->AddTableField("animationDeduplicatedRequests", deformationStats.deduplicatedRequests);
        zenith->AddTableField("animationPaletteMatrices", deformationStats.paletteMatrices);
        zenith->AddTableField("animationSkinnedVertices", deformationStats.deformedVertices);
        zenith->AddTableField("animationStaticFallbacks", deformationStats.staticFallbacks);
        zenith->AddTableField("animationQueueOverflows", deformationStats.queueOverflows);
        zenith->AddTableField("animationDispatchOverflows", deformationStats.dispatchOverflows);
        zenith->AddTableField("animationPaletteOverflows", deformationStats.paletteOverflows);
        zenith->AddTableField("animationArenaOverflows", deformationStats.deformationArenaOverflows);
        zenith->AddTableField("animationControllerAdvances", playbackStats.controllerAdvances);
        zenith->AddTableField("animationControllerPoseRequests", playbackStats.poseRequests);
        zenith->AddTableField("animationControllerPoseEvaluations", playbackStats.poseEvaluations);
        zenith->AddTableField("animationControllerEvaluationUS", playbackStats.evaluationMicroseconds);
        for (u32 lod = 0; lod < 8; ++lod)
        {
            const std::string field = "modelLOD" + std::to_string(lod);
            zenith->AddTableField(field.c_str(), modelStats.lodSelections[lod]);
        }
        return 1;
    }
}
