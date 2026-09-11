#include "AssetHandler.h"
#include "Game-Lib/Rendering/Animation/Asset/AnimationAssetDocument.h"
#include <json/json.hpp>
#include "Game-Lib/Rendering/Animation/Controller/AnimationGraphDocument.h"
#include "Game-Lib/Rendering/Animation/Debug/SkeletonDebugDraw.h"
#include "Game-Lib/Application/EnttRegistries.h"
#include "Game-Lib/ECS/Components/AABB.h"
#include "Game-Lib/ECS/Components/Camera.h"
#include "Game-Lib/ECS/Components/Model.h"
#include "Game-Lib/ECS/Components/Name.h"
#include "Game-Lib/ECS/Components/UnitCustomization.h"
#include "Game-Lib/ECS/Components/DisplayInfo.h"
#include "Game-Lib/ECS/Components/UnitEquipment.h"
#include "Game-Lib/ECS/Singletons/Database/ClientDBSingleton.h"
#include "Game-Lib/ECS/Singletons/Database/UnitCustomizationSingleton.h"
#include "Game-Lib/ECS/Util/Database/UnitCustomizationUtil.h"
#include "Game-Lib/Util/UnitUtil.h"
#include <MetaGen/Shared/ClientDB/ClientDB.h>
#include "Game-Lib/Rendering/Texture/TextureRenderer.h"
#include <Renderer/Renderer.h>
#include "Game-Lib/ECS/Singletons/ActiveCamera.h"
#include "Game-Lib/ECS/Singletons/EditorSelection.h"
#include "Game-Lib/ECS/Util/Transforms.h"
#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Rendering/Debug/DebugRenderer.h"
#include "Game-Lib/Rendering/Animation/Deformation/ModelDeformationSystem.h"
#include "Game-Lib/Rendering/Animation/Controller/AnimationSemantic.h"
#include "Game-Lib/Rendering/Animation/Pose/AnimationPose.h"
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
#include "Game-Lib/Util/AssetWriter.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Base/Util/DebugHandler.h>

#include <Scripting/LuaManager.h>
#include <Scripting/Zenith.h>

#include <Filesystem/PactStorage.h>

#include <entt/entt.hpp>
#include <lualib.h>

#include <chrono>
#include <cmath>

#include <glm/gtx/euler_angles.hpp>
#include <glm/gtx/norm.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

namespace Scripting::Asset
{
    struct AnimationPreviewBinding
    {
        Animation::DirectPlaybackHandle playback;
        Animation::ControllerPlaybackHandle controller;
        Animation::PoseSourceHandle poseSource;
        AnimationLoading::SkeletonHandle restPoseSkeleton;
        std::vector<Animation::AnimationParameterHandle> parameterHandles;
        std::shared_ptr<const Animation::EditableAnimationGraph> graphSource;
        std::shared_ptr<const Animation::CompiledAnimationGraph> compiledGraph;
        std::unique_ptr<Animation::AnimationGraphDocument> document;
        std::shared_ptr<const Animation::EditableAnimationGraph> pendingSource;
        std::shared_ptr<const Animation::CompiledAnimationGraph> pendingCompiled;
        std::string authoringStatus;
        u64 publicationRevision = 0;
        std::unique_ptr<Animation::AnimationAssetDocument> assetDocument;
        std::string assetDocumentPath;
        std::string assetDocumentStatus;
        FileFormat::AssetID authoredAssetID = FileFormat::INVALID_ASSET_ID;
    };

    static std::unordered_map<u32, std::shared_ptr<AnimationPreviewBinding>> s_animationBindingByEntity;
    static void RefreshGraphPublication(Animation::ModelDeformationSystem& deformation, AnimationPreviewBinding& binding);

    struct AnimationPreviewAttachment
    {
        u32 parentEntity = 0;
        u32 childEntity = 0;
        Animation::PoseAttachmentHandle attachment;
        mat4x4 childLocalTransform = mat4x4(1.0f);
    };

    static std::unordered_map<u32, AnimationPreviewAttachment> s_animationAttachmentByChild;

    static void ReleaseModelAttachment(u32 childEntity)
    {
        const auto found = s_animationAttachmentByChild.find(childEntity);
        if (found == s_animationAttachmentByChild.end())
            return;
        GameRenderer* gameRenderer = ServiceLocator::GetGameRenderer();
        Animation::ModelDeformationSystem* deformation = gameRenderer ? gameRenderer->GetModelRenderSystem()->GetDeformationSystem() : nullptr;
        const Animation::PoseAttachmentHandle attachment = found->second.attachment;
        s_animationAttachmentByChild.erase(found);
        if (deformation)
        {
            deformation->UnbindModelAttachment(attachment);
            deformation->DestroyAttachment(attachment);
        }
    }

    static void ReleaseChildAttachments(u32 parentEntity)
    {
        std::vector<u32> children;
        for (const auto& [childEntity, attachment] : s_animationAttachmentByChild)
            if (attachment.parentEntity == parentEntity)
                children.push_back(childEntity);
        for (u32 childEntity : children)
            ReleaseModelAttachment(childEntity);
    }

    static void ReleaseAnimationBinding(u32 entityValue, bool clearSceneBinding)
    {
        const auto found = s_animationBindingByEntity.find(entityValue);
        if (found == s_animationBindingByEntity.end())
            return;
        GameRenderer* gameRenderer = ServiceLocator::GetGameRenderer();
        Animation::ModelDeformationSystem* deformation = gameRenderer ? gameRenderer->GetModelRenderSystem()->GetDeformationSystem() : nullptr;
        if (clearSceneBinding && gameRenderer)
            gameRenderer->GetModelSceneBridge()->SetPoseSource(static_cast<entt::entity>(entityValue), {});
        std::shared_ptr<AnimationPreviewBinding> binding = found->second;
        s_animationBindingByEntity.erase(found);
        if (binding.use_count() != 1 || !deformation)
            return;
        if (binding->playback)
            deformation->DestroyDirectPlayback(binding->playback);
        else if (binding->controller)
            deformation->DestroyControllerPlayback(binding->controller);
        else if (binding->poseSource)
        {
            deformation->DestroyPoseSource(binding->poseSource);
            if (binding->restPoseSkeleton && gameRenderer)
                gameRenderer->GetRenderAssetResources()->GetAnimationRegistry().Release(binding->restPoseSkeleton);
        }
    }

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
        while (!s_animationAttachmentByChild.empty())
            ReleaseModelAttachment(s_animationAttachmentByChild.begin()->first);
        while (!s_animationBindingByEntity.empty())
            ReleaseAnimationBinding(s_animationBindingByEntity.begin()->first, false);
    }

    static fs::path GetDataRoot()
    {
        return fs::absolute("Data");
    }

    static fs::path GetAnimationSourceDirectory()
    {
        auto* writer = ServiceLocator::GetAssetWriter();
        fs::path directory;
        if (!writer || !writer->ResolvePath("AnimationSources", Util::AssetWriteTarget::Disk, directory))
            throw std::runtime_error("Animation source Data directory is unavailable");
        return directory;
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

    i32 AssetHandler::ListDir(Zenith* zenith)
    {
        const char* relativeRaw = zenith->IsString(1) ? zenith->Get<const char*>(1) : "";
        std::string relative = relativeRaw ? relativeRaw : "";
        std::replace(relative.begin(), relative.end(), '\\', '/');
        while (!relative.empty() && relative.front() == '/')
            relative.erase(relative.begin());
        while (!relative.empty() && relative.back() == '/')
            relative.pop_back();
        if (relative.find("..") != std::string::npos)
            relative.clear();

        fs::path dataRoot = GetDataRoot();
        fs::path target = relative.empty() ? dataRoot : (dataRoot / relative);

        struct FolderEntry
        {
        public:
            std::string name;
            std::string path;
            std::set<std::string> files;
            std::set<std::string> folders;
        };

        struct FileEntry
        {
        public:
            std::string name;
            std::string path;
        };

        std::map<std::string, FolderEntry> folders;
        std::map<std::string, FileEntry> files;
        auto getVirtualName = [](const std::string& virtualPath)
        {
            const size_t separator = virtualPath.find_last_of('/');
            return separator == std::string::npos ? virtualPath : virtualPath.substr(separator + 1);
        };

        PACT::PactStorage* pactStorage = ServiceLocator::GetPactStorage();
        if (pactStorage)
        {
            std::vector<std::string> virtualDirectories;
            std::vector<std::string> virtualFiles;
            pactStorage->GetDirectoryEntries(relative, virtualDirectories, virtualFiles);
            for (const std::string& virtualFile : virtualFiles)
            {
                const std::string name = getVirtualName(virtualFile);
                files.try_emplace(name, FileEntry{ .name = name, .path = virtualFile });
            }

            for (const std::string& virtualDirectory : virtualDirectories)
            {
                const std::string name = getVirtualName(virtualDirectory);
                FolderEntry& folder = folders.try_emplace(name, FolderEntry{ .name = name, .path = virtualDirectory }).first->second;

                std::vector<std::string> childDirectories;
                std::vector<std::string> childFiles;
                pactStorage->GetDirectoryEntries(virtualDirectory, childDirectories, childFiles);
                for (const std::string& childDirectory : childDirectories)
                    folder.folders.emplace(getVirtualName(childDirectory));
                for (const std::string& childFile : childFiles)
                    folder.files.emplace(getVirtualName(childFile));
            }
        }

        std::error_code ec;
        if (fs::is_directory(target, ec))
        {
            for (const auto& entry : fs::directory_iterator(target, ec))
            {
                const fs::path& entryPath = entry.path();
                if (entry.is_directory())
                {
                    const std::string name = entryPath.filename().string();
                    FolderEntry& folder = folders.try_emplace(name, FolderEntry{ .name = name, .path = ToDataRelative(entryPath, dataRoot) }).first->second;
                    std::error_code childError;
                    for (const auto& child : fs::directory_iterator(entryPath, childError))
                    {
                        if (child.is_directory())
                            folder.folders.emplace(child.path().filename().string());
                        else
                            folder.files.emplace(child.path().filename().string());
                    }
                }
                else
                {
                    const std::string name = entryPath.filename().string();
                    files.try_emplace(name, FileEntry{ .name = name, .path = ToDataRelative(entryPath, dataRoot) });
                }
            }
        }

        zenith->CreateTable();
        zenith->CreateTable();
        i32 folderIndex = 0;
        for (const auto& [name, folder] : folders)
        {
            zenith->CreateTable();
            zenith->AddTableField("name", name.c_str());
            zenith->AddTableField("path", folder.path.c_str());
            zenith->AddTableField("fileCount", static_cast<u32>(folder.files.size()));
            zenith->AddTableField("folderCount", static_cast<u32>(folder.folders.size()));
            zenith->SetTableKey(++folderIndex);
        }
        zenith->SetTableKey("folders");

        zenith->CreateTable();
        i32 fileIndex = 0;
        for (const auto& [name, file] : files)
        {
            std::string ext = fs::path(name).extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(::tolower(c)); });

            zenith->CreateTable();
            zenith->AddTableField("name", name.c_str());
            zenith->AddTableField("path", file.path.c_str());
            zenith->AddTableField("ext", ext.c_str());
            zenith->SetTableKey(++fileIndex);
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

    i32 AssetHandler::ApplyCharacterAppearance(Zenith* zenith)
    {
        const entt::entity entity = static_cast<entt::entity>(zenith->CheckVal<u32>(1));
        auto* registries = ServiceLocator::GetEnttRegistries();
        auto& registry = *registries->gameRegistry;
        auto* model = registry.valid(entity) ? registry.try_get<ECS::Components::Model>(entity) : nullptr;
        const u32 race = zenith->CheckVal<u32>(2);
        const u32 gender = zenith->CheckVal<u32>(3);
        const u32 skin = zenith->CheckVal<u32>(4);
        const u32 face = zenith->CheckVal<u32>(5);
        const u32 hairStyle = zenith->CheckVal<u32>(6);
        const u32 hairColor = zenith->CheckVal<u32>(7);
        if (!model || !model->flags.loaded || race < 1 || race > 8 || gender < 1 || gender > 2 || skin > 255 || face > 255 || hairStyle > 255 || hairColor > 255)
        {
            zenith->Push(false);
            zenith->Push(!model ? "Missing model" : !model->flags.loaded ? "Model is loading" : "Invalid appearance selection");
            return 2;
        }
        auto& clientDB = registries->dbRegistry->ctx().get<ECS::Singletons::ClientDBSingleton>();
        auto& customization = registries->dbRegistry->ctx().get<ECS::Singletons::UnitCustomizationSingleton>();
        Renderer::TextureID baseSkin;
        const auto* raceRow = clientDB.Get(ClientDBHash::UnitRace)->TryGet<MetaGen::Shared::ClientDB::UnitRaceRecord>(race);
        if (!raceRow || !ECSUtil::UnitCustomization::GetBaseSkinTextureID(customization, static_cast<GameDefine::UnitRace>(race), static_cast<GameDefine::UnitGender>(gender), static_cast<u8>(skin), baseSkin))
        {
            zenith->Push(false);
            zenith->Push("Skin selection missing from customization database");
            return 2;
        }
        auto& display = registry.get_or_emplace<ECS::Components::DisplayInfo>(entity);
        display.race = static_cast<GameDefine::UnitRace>(race);
        display.gender = static_cast<GameDefine::UnitGender>(gender);
        display.displayID = gender == 1 ? raceRow->maleDisplayID : raceRow->femaleDisplayID;
        (void)registry.get_or_emplace<ECS::Components::UnitEquipment>(entity);
        auto& appearance = registry.get_or_emplace<ECS::Components::UnitCustomization>(entity);
        appearance.skinID = static_cast<u8>(skin);
        appearance.faceID = static_cast<u8>(face);
        appearance.hairStyleID = static_cast<u8>(hairStyle);
        appearance.hairColorID = static_cast<u8>(hairColor);
        appearance.flags.useCustomSkin = true;
        appearance.flags.forceRefresh = true;
        ::Util::Unit::RefreshGeometryGroups(registry, entity, clientDB, customization, *model);
        ::Util::Unit::RefreshSkinTexture(registry, entity, clientDB, customization, display, appearance, *model);
        zenith->Push(true);
        zenith->Push("Appearance applied");
        return 2;
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
        if (!poseSource)
        {
            registry.Release(skeleton);
            zenith->Push(false);
            return 1;
        }
        ReleaseAnimationBinding(entityValue, true);
        if (!bridge->SetPoseSource(entity, poseSource))
        {
            deformation->DestroyPoseSource(poseSource);
            registry.Release(skeleton);
            zenith->Push(false);
            return 1;
        }
        auto binding = std::make_shared<AnimationPreviewBinding>();
        binding->poseSource = poseSource;
        binding->restPoseSkeleton = skeleton;
        s_animationBindingByEntity[entityValue] = std::move(binding);
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
        if (!playback || !poseSource)
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
        ReleaseAnimationBinding(entityValue, true);
        if (!bridge->SetPoseSource(entity, poseSource))
        {
            deformation->DestroyDirectPlayback(playback);
            zenith->Push(false);
            return 1;
        }
        auto binding = std::make_shared<AnimationPreviewBinding>();
        binding->playback = playback;
        binding->poseSource = poseSource;
        s_animationBindingByEntity[entityValue] = std::move(binding);
        bridge->SetShadowDynamic(entity, true);
        NC_LOG_INFO("ANIMATION_PREVIEW clip_bound entity={} skeleton={} clip={} joints={} tracks={} playback={}", entityValue, skeletonPath, clipPath, skeletonView->joints.size(), clipView->tracks.size(), playback.GetRawValue());
        zenith->Push(true);
        return 1;
    }

    i32 AssetHandler::ShareAnimationPose(Zenith* zenith)
    {
        const u32 targetEntityValue = zenith->CheckVal<u32>(1);
        const u32 sourceEntityValue = zenith->CheckVal<u32>(2);
        if (targetEntityValue == sourceEntityValue)
        {
            zenith->Push(s_animationBindingByEntity.contains(sourceEntityValue));
            return 1;
        }
        const auto source = s_animationBindingByEntity.find(sourceEntityValue);
        GameRenderer* gameRenderer = ServiceLocator::GetGameRenderer();
        ModelScene::ModelSceneBridge* bridge = gameRenderer ? gameRenderer->GetModelSceneBridge() : nullptr;
        const entt::entity targetEntity = static_cast<entt::entity>(targetEntityValue);
        if (source == s_animationBindingByEntity.end() || !bridge || !source->second->poseSource || !bridge->GetScene(targetEntity))
        {
            zenith->Push(false);
            return 1;
        }
        const std::shared_ptr<AnimationPreviewBinding> shared = source->second;
        ReleaseAnimationBinding(targetEntityValue, true);
        if (!bridge->SetPoseSource(targetEntity, shared->poseSource))
        {
            zenith->Push(false);
            return 1;
        }
        s_animationBindingByEntity[targetEntityValue] = shared;
        bridge->SetShadowDynamic(targetEntity, true);
        NC_LOG_INFO("ANIMATION_PREVIEW pose_shared source_entity={} target_entity={} pose_source={} references={}", sourceEntityValue, targetEntityValue, shared->poseSource.GetRawValue(), shared.use_count());
        zenith->Push(true);
        return 1;
    }

    static Animation::ModelDeformationSystem* GetPlayback(u32 entityValue, Animation::DirectPlaybackHandle& playback)
    {
        const auto found = s_animationBindingByEntity.find(entityValue);
        GameRenderer* gameRenderer = ServiceLocator::GetGameRenderer();
        if (found == s_animationBindingByEntity.end() || !gameRenderer || !found->second->playback)
            return nullptr;
        playback = found->second->playback;
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

    struct PreviewAnimationGraph
    {
        Animation::EditableAnimationGraph source;
        std::shared_ptr<const Animation::CompiledAnimationGraph> compiled;
    };

    static PreviewAnimationGraph CompilePreviewLocomotionGraph(std::string_view mode)
    {
        Animation::EditableAnimationGraph graph;
        if (mode.starts_with("slot:") && mode.size() > 5)
        {
            graph.states = {{1, Animation::HashAnimationSlot(mode.substr(5)), 60000000}};
            graph.states[0].displayName = std::string(mode.substr(5));
            graph.entryStateGUID = 1;
        }
        else if (mode == "retarget_layers")
        {
            graph.parameters = {{"upperBodyWeight", FileFormat::Animation::GraphValueType::Float}};
            graph.states = {{1, Animation::HashAnimationSlot("WalkForward"), 60000000}};
            graph.states[0].displayName = "Renamed rig / walk";
            graph.entryStateGUID = 1;
            Animation::EditableGraphLayer layer;
            layer.sourceGUID = 2;
            layer.clipSlotSemanticID = Animation::HashAnimationSlot("UpperBody");
            layer.maskAssetID = Util::AssetPath::Hash("animation/retarget/upper.bonemask");
            layer.durationMicroseconds = 60000000;
            layer.weightParameter = "upperBodyWeight";
            layer.displayName = "Reference rig / running arms";
            graph.layers = {layer};
            graph.displayName = "Cross-domain masked layer";
        }
        else if (mode == "ik" || mode == "ik_showcase")
        {
            Animation::EditableGraphParameter goal;
            goal.name = "rightHandGoal";
            goal.type = FileFormat::Animation::GraphValueType::Float3;
            goal.defaultFloat3 = vec3(-0.55f, 1.25f, 0.30f);
            Animation::EditableGraphParameter weight;
            weight.name = "ikWeight";
            weight.type = FileFormat::Animation::GraphValueType::Float;
            weight.defaultFloat = 1.0f;
            graph.parameters = {goal, weight};
            if (mode == "ik_showcase")
            {
                Animation::EditableGraphParameter headGoal;
                headGoal.name = "headGoal";
                headGoal.type = FileFormat::Animation::GraphValueType::Float3;
                headGoal.defaultFloat3 = vec3(0.0f, 1.65f, 1.0f);
                graph.parameters.push_back(headGoal);
            }
            graph.states = {{1, Animation::HashAnimationSlot("Idle"), 60'000'000}};
            graph.entryStateGUID = 1;
            Animation::EditableGraphIKOperation ik;
            ik.sourceGUID = 2;
            ik.rigAssetID = Util::AssetPath::Hash("character.ikrig");
            ik.solverSemanticID = Animation::HashAnimationSemantic("ik-solver", "right_hand");
            ik.goalParameter = "rightHandGoal";
            ik.weightParameter = "ikWeight";
            ik.displayName = "Right Hand IK";
            graph.ikOperations = {ik};
            if (mode == "ik_showcase")
            {
                Animation::EditableGraphIKOperation look;
                look.sourceGUID = 3;
                look.rigAssetID = Util::AssetPath::Hash("character.ikrig");
                look.solverSemanticID = Animation::HashAnimationSemantic("ik-solver", "head_look");
                look.goalParameter = "headGoal";
                look.constantWeight = 0.65f;
                look.executionOrder = 1;
                look.displayName = "Constrained Head Look";
                graph.ikOperations.push_back(look);
            }
            graph.displayName = "Target Skeleton IK";
            graph.states[0].displayName = mode == "ik_showcase" ? "Idle + Hand Reach + Head Look" : "Idle + Right Hand IK";
            graph.states[0].editorPosition = {0.0f, 0.0f};
        }
        else if (mode == "layers")
        {
            graph.parameters = {{"speed", FileFormat::Animation::GraphValueType::Float}, {"upperBodyWeight", FileFormat::Animation::GraphValueType::Float}, {"additiveWeight", FileFormat::Animation::GraphValueType::Float}};
            Animation::EditableGraphState locomotion;
            locomotion.sourceGUID = 31;
            locomotion.durationMicroseconds = 60'000'000;
            locomotion.kind = Animation::GraphStateKind::BlendSpace1D;
            locomotion.inputX = "speed";
            locomotion.smoothingHalfLifeSeconds = 0.12f;
            locomotion.blendSamples = {{{0.0f, 0.0f}, Animation::HashAnimationSlot("Idle")}, {{1.0f, 0.0f}, Animation::HashAnimationSlot("WalkForward")}, {{3.0f, 0.0f}, Animation::HashAnimationSlot("RunForward")}};
            locomotion.displayName = "Base Locomotion";
            graph.states = {locomotion};
            graph.entryStateGUID = locomotion.sourceGUID;
            Animation::EditableGraphLayer upperBody;
            upperBody.sourceGUID = 32;
            upperBody.clipSlotSemanticID = Animation::HashAnimationSlot("AttackUnarmed");
            upperBody.maskAssetID = Util::AssetPath::Hash("character_upper_body.bonemask");
            upperBody.durationMicroseconds = 1'033'333;
            upperBody.type = Animation::AnimationLayerType::Override;
            upperBody.weightParameter = "upperBodyWeight";
            upperBody.executionOrder = 0;
            upperBody.displayName = "Upper Body Punch Override";
            Animation::EditableGraphLayer additive;
            additive.sourceGUID = 33;
            additive.clipSlotSemanticID = Animation::HashAnimationSlot("BreathingAdditive");
            additive.maskAssetID = Util::AssetPath::Hash("character_upper_body.bonemask");
            additive.durationMicroseconds = 9'933'333;
            additive.type = Animation::AnimationLayerType::Additive;
            additive.weightParameter = "additiveWeight";
            additive.executionOrder = 1;
            additive.displayName = "Breathing Additive";
            graph.layers = {upperBody, additive};
            graph.outputs = {{Animation::HashAnimationSemantic("animation-output", "upper_body_weight"), "upperBodyWeight"}, {Animation::HashAnimationSemantic("animation-output", "additive_weight"), "additiveWeight"}};
            graph.displayName = "Masked Override + Additive";
        }
        else if (mode == "actions" || mode == "attachment")
        {
            graph.parameters = {{"speed", FileFormat::Animation::GraphValueType::Float}};
            if (mode == "attachment")
            {
                Animation::EditableGraphParameter leftGripGoal;
                leftGripGoal.name = "leftGripGoal";
                leftGripGoal.type = FileFormat::Animation::GraphValueType::Float3;
                Animation::EditableGraphParameter leftGripWeight;
                leftGripWeight.name = "leftGripWeight";
                leftGripWeight.type = FileFormat::Animation::GraphValueType::Float;
                graph.parameters.push_back(leftGripGoal);
                graph.parameters.push_back(leftGripWeight);
            }
            Animation::EditableGraphState locomotion;
            locomotion.sourceGUID = 41;
            locomotion.durationMicroseconds = 60'000'000;
            locomotion.kind = Animation::GraphStateKind::BlendSpace1D;
            locomotion.inputX = "speed";
            locomotion.smoothingHalfLifeSeconds = 0.1f;
            locomotion.blendSamples = {{{0.0f, 0.0f}, Animation::HashAnimationSlot("Idle")}, {{1.0f, 0.0f}, Animation::HashAnimationSlot("WalkForward")}, {{3.0f, 0.0f}, Animation::HashAnimationSlot("RunForward")}};
            locomotion.displayName = "Continuing Locomotion";
            graph.states = {locomotion};
            graph.entryStateGUID = locomotion.sourceGUID;
            const FileFormat::AssetID upperBodyMask = Util::AssetPath::Hash("character_upper_body.bonemask");
            Animation::EditableGraphActionSlot punch;
            punch.semanticID = Animation::HashAnimationSemantic("animation-action", "punch");
            punch.clipSlotSemanticID = Animation::HashAnimationSlot("AttackUnarmed");
            punch.maskAssetID = upperBodyMask;
            punch.durationMicroseconds = 1'033'333;
            punch.blendInMicroseconds = 120'000;
            punch.blendOutMicroseconds = 160'000;
            punch.priority = 1;
            punch.displayName = "Punch";
            Animation::EditableGraphActionSlot sword;
            sword.semanticID = Animation::HashAnimationSemantic("animation-action", "sword_combo");
            sword.clipSlotSemanticID = Animation::HashAnimationSlot("SwordAttack");
            sword.maskAssetID = upperBodyMask;
            sword.durationMicroseconds = 3'533'333;
            sword.blendInMicroseconds = 180'000;
            sword.blendOutMicroseconds = 220'000;
            sword.priority = 2;
            sword.displayName = "Sword Combo";
            Animation::EditableGraphActionSlot reaction;
            reaction.semanticID = Animation::HashAnimationSemantic("animation-action", "hit_reaction");
            reaction.clipSlotSemanticID = Animation::HashAnimationSlot("HitReaction");
            reaction.maskAssetID = upperBodyMask;
            reaction.durationMicroseconds = 1'266'667;
            reaction.blendInMicroseconds = 80'000;
            reaction.blendOutMicroseconds = 180'000;
            reaction.priority = 3;
            reaction.queueWhenBlocked = false;
            reaction.displayName = "Hit Reaction";
            graph.actionSlots = {punch, sword, reaction};
            if (mode == "attachment")
            {
                Animation::EditableGraphLayer grip;
                grip.sourceGUID = 43;
                grip.clipSlotSemanticID = Animation::HashAnimationSlot("SwordReady");
                grip.maskAssetID = upperBodyMask;
                grip.durationMicroseconds = 33333;
                grip.type = Animation::AnimationLayerType::Override;
                grip.executionOrder = 0;
                grip.displayName = "Two-Hand Sword Ready";
                graph.layers = {grip};
                Animation::EditableGraphIKOperation secondaryGrip;
                secondaryGrip.sourceGUID = 44;
                secondaryGrip.rigAssetID = Util::AssetPath::Hash("character.ikrig");
                secondaryGrip.solverSemanticID = Animation::HashAnimationSemantic("ik-solver", "left_hand");
                secondaryGrip.goalParameter = "leftGripGoal";
                secondaryGrip.goalAttachmentSemanticID = Animation::HashAnimationSemantic("skeleton-attachment", "right_hand");
                secondaryGrip.effectorAttachmentSemanticID = Animation::HashAnimationSemantic("skeleton-attachment", "left_hand");
                secondaryGrip.effectorAlignmentAxis = vec3(1.0f, 0.0f, 0.0f);
                secondaryGrip.weightParameter = "leftGripWeight";
                secondaryGrip.displayName = "Off-Hand Weapon Grip";
                graph.ikOperations = {secondaryGrip};
                graph.displayName = "Weapon Attachment + Grip";
            }
            else
            {
                graph.displayName = "Priority Action Layer";
            }
        }
        else if (mode == "blend1d")
        {
            graph.parameters = {{"speed", FileFormat::Animation::GraphValueType::Float}};
            Animation::EditableGraphState locomotion;
            locomotion.sourceGUID = 11;
            locomotion.durationMicroseconds = 60'000'000;
            locomotion.kind = Animation::GraphStateKind::BlendSpace1D;
            locomotion.inputX = "speed";
            locomotion.smoothingHalfLifeSeconds = 0.12f;
            locomotion.blendSamples = {{{1.0f, 0.0f}, Animation::HashAnimationSlot("WalkForward")}, {{3.0f, 0.0f}, Animation::HashAnimationSlot("RunForward")}};
            graph.states = {{10, Animation::HashAnimationSlot("Idle"), 60'000'000}, locomotion};
            graph.entryStateGUID = 10;
            graph.transitions = {{10, 11, "speed", Animation::GraphComparison::Greater, 0.1f, 250'000, Animation::BlendCurve::SmoothStep}, {11, 10, "speed", Animation::GraphComparison::LessEqual, 0.1f, 250'000, Animation::BlendCurve::SmoothStep}};
            graph.displayName = "Locomotion Blend 1D";
            graph.states[0].displayName = "Idle";
            graph.states[0].editorPosition = {-220.0f, 0.0f};
            graph.states[1].displayName = "Locomotion";
            graph.states[1].editorPosition = {160.0f, 0.0f};
            graph.states[1].blendSamples[0].displayName = "Walk Forward";
            graph.states[1].blendSamples[1].displayName = "Run Forward";
        }
        else if (mode == "blend2d")
        {
            graph.parameters = {{"lateral", FileFormat::Animation::GraphValueType::Float}, {"forward", FileFormat::Animation::GraphValueType::Float}, {"magnitude", FileFormat::Animation::GraphValueType::Float}};
            Animation::EditableGraphState locomotion;
            locomotion.sourceGUID = 21;
            locomotion.durationMicroseconds = 60'000'000;
            locomotion.kind = Animation::GraphStateKind::BlendSpace2D;
            locomotion.inputX = "lateral";
            locomotion.inputY = "forward";
            locomotion.smoothingHalfLifeSeconds = 0.12f;
            locomotion.blendSamples = {{{-1.0f, 0.0f}, Animation::HashAnimationSlot("StrafeLeft")}, {{1.0f, 0.0f}, Animation::HashAnimationSlot("StrafeRight")}, {{0.0f, 1.0f}, Animation::HashAnimationSlot("RunForward")}, {{0.0f, -1.0f}, Animation::HashAnimationSlot("RunBackward")}};
            locomotion.blendTriangles = {{{{0, 1, 2}}}, {{{0, 3, 1}}}};
            graph.states = {{20, Animation::HashAnimationSlot("Idle"), 60'000'000}, locomotion};
            graph.entryStateGUID = 20;
            graph.transitions = {{20, 21, "magnitude", Animation::GraphComparison::Greater, 0.1f, 250'000, Animation::BlendCurve::SmoothStep}, {21, 20, "magnitude", Animation::GraphComparison::LessEqual, 0.1f, 250'000, Animation::BlendCurve::SmoothStep}};
            graph.displayName = "Locomotion Blend 2D";
            graph.states[0].displayName = "Idle";
            graph.states[0].editorPosition = {-220.0f, 0.0f};
            graph.states[1].displayName = "Directional Locomotion";
            graph.states[1].editorPosition = {160.0f, 0.0f};
            graph.states[1].blendSamples[0].displayName = "Strafe Left";
            graph.states[1].blendSamples[1].displayName = "Strafe Right";
            graph.states[1].blendSamples[2].displayName = "Run Forward";
            graph.states[1].blendSamples[3].displayName = "Run Backward";
        }
        else
        {
            graph.parameters = {{"speed", FileFormat::Animation::GraphValueType::Float}};
            graph.states = {{1, Animation::HashAnimationSlot("Idle"), 60'000'000}, {2, Animation::HashAnimationSlot("WalkForward"), 60'000'000}, {3, Animation::HashAnimationSlot("RunForward"), 60'000'000}};
            graph.entryStateGUID = 1;
            graph.transitions = {
                {1, 2, "speed", Animation::GraphComparison::Greater, 0.1f, 250'000, Animation::BlendCurve::SmoothStep},
                {2, 1, "speed", Animation::GraphComparison::LessEqual, 0.1f, 250'000, Animation::BlendCurve::SmoothStep},
                {2, 3, "speed", Animation::GraphComparison::Greater, 2.0f, 200'000, Animation::BlendCurve::SmoothStep},
                {3, 2, "speed", Animation::GraphComparison::LessEqual, 2.0f, 200'000, Animation::BlendCurve::SmoothStep},
            };
            graph.displayName = "Locomotion State Machine";
            graph.states[0].displayName = "Idle";
            graph.states[0].editorPosition = {-300.0f, 0.0f};
            graph.states[1].displayName = "Walk Forward";
            graph.states[1].editorPosition = {0.0f, 0.0f};
            graph.states[2].displayName = "Run Forward";
            graph.states[2].editorPosition = {300.0f, 0.0f};
        }
        const Animation::GraphCompileResult result = Animation::CompileAnimationGraph(graph);
        return {std::move(graph), result ? result.graph : nullptr};
    }

    i32 AssetHandler::BindAnimationController(Zenith* zenith)
    {
        const u32 entityValue = zenith->CheckVal<u32>(1);
        const char* skeletonPathRaw = zenith->CheckVal<const char*>(2);
        const char* setPathRaw = zenith->CheckVal<const char*>(3);
        const std::string_view mode = zenith->IsString(4) ? zenith->Get<const char*>(4) : "states";
        GameRenderer* gameRenderer = ServiceLocator::GetGameRenderer();
        ModelScene::ModelSceneBridge* bridge = gameRenderer ? gameRenderer->GetModelSceneBridge() : nullptr;
        const entt::entity entity = static_cast<entt::entity>(entityValue);
        RenderScenes::RenderScene* scene = bridge ? bridge->GetScene(entity) : nullptr;
        Animation::ModelDeformationSystem* deformation = scene ? scene->GetModelDeformationSystem() : nullptr;
        if (!skeletonPathRaw || !setPathRaw || !deformation)
        {
            zenith->Push(false);
            return 1;
        }
        PACT::PactStorage* pactStorage = ServiceLocator::GetPactStorage();
        std::string skeletonPath;
        std::string setPath;
        FileFormat::AssetID skeletonAssetID = FileFormat::INVALID_ASSET_ID;
        FileFormat::AssetID setAssetID = FileFormat::INVALID_ASSET_ID;
        if (!pactStorage || !ResolveModelAssetPath(skeletonPathRaw, pactStorage, skeletonPath, skeletonAssetID) || !ResolveModelAssetPath(setPathRaw, pactStorage, setPath, setAssetID))
        {
            zenith->Push(false);
            return 1;
        }
        AnimationLoading::AnimationAssetRegistry& assets = gameRenderer->GetRenderAssetResources()->GetAnimationRegistry();
        const AnimationLoading::SkeletonHandle skeleton = assets.LoadSkeleton(skeletonAssetID);
        const AnimationLoading::AnimationSetHandle animationSet = assets.LoadAnimationSet(setAssetID);
        AssetLoading::Diagnostic skeletonDiagnostic;
        AssetLoading::Diagnostic setDiagnostic;
        const AnimationLoading::SkeletonAssetView* skeletonView = assets.Get(skeleton, &skeletonDiagnostic);
        const AnimationLoading::AnimationSetAssetView* setView = assets.Get(animationSet, &setDiagnostic);
        PreviewAnimationGraph graph = CompilePreviewLocomotionGraph(mode);
        std::shared_ptr<AnimationPreviewBinding> previousBinding;
        const bool preserveDrafts = zenith->IsBoolean(6) && zenith->Get<bool>(6);
        if (const auto previous = s_animationBindingByEntity.find(entityValue); previous != s_animationBindingByEntity.end())
            previousBinding = previous->second;
        if (preserveDrafts && previousBinding && previousBinding->compiledGraph && previousBinding->graphSource)
        {
            RefreshGraphPublication(*deformation, *previousBinding);
            if (previousBinding->pendingCompiled)
            {
                assets.Release(animationSet);
                assets.Release(skeleton);
                zenith->Push(false);
                return 1;
            }
            graph = {*previousBinding->graphSource, previousBinding->compiledGraph};
        }
        if (!skeletonView || !setView || !graph.compiled)
        {
            assets.Release(animationSet);
            assets.Release(skeleton);
            NC_LOG_ERROR("ANIMATION_CONTROLLER bind_failed entity={} skeleton={} skeleton_diagnostic={} set={} set_diagnostic={}", entityValue, skeletonPath, AssetLoading::Describe(skeletonDiagnostic), setPath, AssetLoading::Describe(setDiagnostic));
            zenith->Push(false);
            return 1;
        }
        FileFormat::AssetID retargetProfileAssetID = FileFormat::INVALID_ASSET_ID;
        if (zenith->IsString(5))
        {
            std::string profilePath;
            if (!ResolveModelAssetPath(zenith->Get<const char*>(5), pactStorage, profilePath, retargetProfileAssetID))
            {
                assets.Release(skeleton);
                assets.Release(animationSet);
                zenith->Push(false);
                return 1;
            }
        }
        const Animation::ControllerPlaybackHandle controller = deformation->CreateControllerPlayback(skeleton, animationSet, *skeletonView, *setView, graph.compiled, entityValue, retargetProfileAssetID);
        const Animation::PoseSourceHandle poseSource = deformation->GetControllerPoseSource(controller);
        if (!controller || !poseSource)
        {
            assets.Release(animationSet);
            assets.Release(skeleton);
            zenith->Push(false);
            return 1;
        }
        auto binding = std::make_shared<AnimationPreviewBinding>();
        binding->controller = controller;
        binding->poseSource = poseSource;
        binding->graphSource = std::make_shared<const Animation::EditableAnimationGraph>(std::move(graph.source));
        binding->compiledGraph = std::move(graph.compiled);
        if (previousBinding)
        {
            binding->assetDocument = std::move(previousBinding->assetDocument);
            binding->assetDocumentPath = std::move(previousBinding->assetDocumentPath);
            binding->assetDocumentStatus = std::move(previousBinding->assetDocumentStatus);
            binding->authoredAssetID = previousBinding->authoredAssetID;
            if (preserveDrafts)
            {
                binding->document = std::move(previousBinding->document);
                binding->publicationRevision = previousBinding->publicationRevision + 1;
                binding->authoringStatus = "Comparison restarted playback; graph and asset drafts retained";
            }
        }
        previousBinding.reset();
        ReleaseAnimationBinding(entityValue, true);
        if (!bridge->SetPoseSource(entity, poseSource))
        {
            deformation->DestroyControllerPlayback(controller);
            zenith->Push(false);
            return 1;
        }
        s_animationBindingByEntity[entityValue] = std::move(binding);
        NC_LOG_INFO("ANIMATION_CONTROLLER bound entity={} skeleton={} set={} states={} parameters={} pose_source={}", entityValue, skeletonPath, setPath, s_animationBindingByEntity[entityValue]->compiledGraph->states.size(), s_animationBindingByEntity[entityValue]->compiledGraph->parameters.size(), poseSource.GetRawValue());
        zenith->Push(true);
        return 1;
    }

    static Animation::ModelDeformationSystem* GetControllerBinding(u32 entityValue, std::shared_ptr<AnimationPreviewBinding>& binding, bool requireController = true)
    {
        const auto found = s_animationBindingByEntity.find(entityValue);
        GameRenderer* gameRenderer = ServiceLocator::GetGameRenderer();
        if (found == s_animationBindingByEntity.end() || !gameRenderer || (requireController && !found->second->controller))
            return nullptr;
        binding = found->second;
        return gameRenderer->GetModelRenderSystem()->GetDeformationSystem();
    }

    i32 AssetHandler::StepAnimationControllers(Zenith* zenith)
    {
        const f64 delta = zenith->CheckVal<f64>(1);
        GameRenderer* renderer = ServiceLocator::GetGameRenderer();
        auto* deformation = renderer ? renderer->GetModelRenderSystem()->GetDeformationSystem() : nullptr;
        const bool valid = deformation && std::isfinite(delta) && delta >= 0.0 && delta <= 0.1;
        if (valid)
            deformation->StepPreviewControllers(delta);
        zenith->Push(valid);
        return 1;
    }

    i32 AssetHandler::ResolveAnimationParameter(Zenith* zenith)
    {
        std::shared_ptr<AnimationPreviewBinding> binding;
        Animation::ModelDeformationSystem* deformation = GetControllerBinding(zenith->CheckVal<u32>(1), binding);
        const char* name = zenith->CheckVal<const char*>(2);
        const Animation::AnimationParameterHandle handle = deformation && name ? deformation->ResolveControllerParameter(binding->controller, Animation::HashAnimationName(name)) : Animation::AnimationParameterHandle{};
        if (!handle)
        {
            zenith->Push();
            return 1;
        }
        if (binding->parameterHandles.size() <= handle.parameterIndex)
            binding->parameterHandles.resize(static_cast<size_t>(handle.parameterIndex) + 1);
        binding->parameterHandles[handle.parameterIndex] = handle;
        zenith->Push((static_cast<u64>(handle.graphGeneration) << 32) | (static_cast<u64>(handle.parameterIndex) + 1));
        return 1;
    }

    static Animation::AnimationParameterHandle GetParameterToken(const std::shared_ptr<AnimationPreviewBinding>& binding, u64 token)
    {
        const u32 index = static_cast<u32>(token);
        if (!binding || index == 0 || index > binding->parameterHandles.size()) return {};
        const auto handle = binding->parameterHandles[index - 1];
        return handle.graphGeneration == static_cast<u32>(token >> 32) ? handle : Animation::AnimationParameterHandle{};
    }

    i32 AssetHandler::AnimationSetFloatParameter(Zenith* zenith)
    {
        std::shared_ptr<AnimationPreviewBinding> binding;
        Animation::ModelDeformationSystem* deformation = GetControllerBinding(zenith->CheckVal<u32>(1), binding);
        zenith->Push(deformation && deformation->SetControllerFloat(GetParameterToken(binding, zenith->CheckVal<u64>(2)), zenith->CheckVal<f32>(3)));
        return 1;
    }

    i32 AssetHandler::AnimationSetFloat3Parameter(Zenith* zenith)
    {
        std::shared_ptr<AnimationPreviewBinding> binding;
        Animation::ModelDeformationSystem* deformation = GetControllerBinding(zenith->CheckVal<u32>(1), binding);
        const vec3 value(zenith->CheckVal<f32>(3), zenith->CheckVal<f32>(4), zenith->CheckVal<f32>(5));
        zenith->Push(deformation && deformation->SetControllerFloat3(GetParameterToken(binding, zenith->CheckVal<u64>(2)), value));
        return 1;
    }

    i32 AssetHandler::AnimationSetBoolParameter(Zenith* zenith)
    {
        std::shared_ptr<AnimationPreviewBinding> binding;
        Animation::ModelDeformationSystem* deformation = GetControllerBinding(zenith->CheckVal<u32>(1), binding);
        zenith->Push(deformation && deformation->SetControllerBool(GetParameterToken(binding, zenith->CheckVal<u64>(2)), zenith->CheckVal<bool>(3)));
        return 1;
    }

    i32 AssetHandler::AnimationTriggerParameter(Zenith* zenith)
    {
        std::shared_ptr<AnimationPreviewBinding> binding;
        Animation::ModelDeformationSystem* deformation = GetControllerBinding(zenith->CheckVal<u32>(1), binding);
        zenith->Push(deformation && deformation->TriggerController(GetParameterToken(binding, zenith->CheckVal<u64>(2))));
        return 1;
    }

    i32 AssetHandler::AnimationQueueAction(Zenith* zenith)
    {
        std::shared_ptr<AnimationPreviewBinding> binding;
        Animation::ModelDeformationSystem* deformation = GetControllerBinding(zenith->CheckVal<u32>(1), binding);
        const char* name = zenith->CheckVal<const char*>(2);
        const u32 payload = zenith->IsNumber(3) ? zenith->Get<u32>(3) : 0;
        zenith->Push(deformation && name && deformation->QueueControllerAction(binding->controller, Animation::HashAnimationSemantic("animation-action", name), payload));
        return 1;
    }

    i32 AssetHandler::BindModelAttachment(Zenith* zenith)
    {
        const u32 parentEntityValue = zenith->CheckVal<u32>(1);
        const u32 childEntityValue = zenith->CheckVal<u32>(2);
        const char* skeletonPathRaw = zenith->CheckVal<const char*>(3);
        const char* semanticName = zenith->CheckVal<const char*>(4);
        const vec3 translation(
            zenith->IsNumber(5) ? zenith->Get<f32>(5) : 0.0f,
            zenith->IsNumber(6) ? zenith->Get<f32>(6) : 0.0f,
            zenith->IsNumber(7) ? zenith->Get<f32>(7) : 0.0f);
        const vec3 rotationDegrees(
            zenith->IsNumber(8) ? zenith->Get<f32>(8) : 0.0f,
            zenith->IsNumber(9) ? zenith->Get<f32>(9) : 0.0f,
            zenith->IsNumber(10) ? zenith->Get<f32>(10) : 0.0f);
        const f32 uniformScale = zenith->IsNumber(11) ? zenith->Get<f32>(11) : 1.0f;
        GameRenderer* gameRenderer = ServiceLocator::GetGameRenderer();
        ModelScene::ModelSceneBridge* bridge = gameRenderer ? gameRenderer->GetModelSceneBridge() : nullptr;
        const auto parentBinding = s_animationBindingByEntity.find(parentEntityValue);
        const entt::entity parentEntity = static_cast<entt::entity>(parentEntityValue);
        const entt::entity childEntity = static_cast<entt::entity>(childEntityValue);
        RenderScenes::RenderScene* parentScene = bridge ? bridge->GetScene(parentEntity) : nullptr;
        RenderScenes::RenderScene* childScene = bridge ? bridge->GetScene(childEntity) : nullptr;
        Animation::ModelDeformationSystem* deformation = parentScene ? parentScene->GetModelDeformationSystem() : nullptr;
        if (!skeletonPathRaw || !semanticName || !deformation || parentScene != childScene || parentBinding == s_animationBindingByEntity.end() || s_animationAttachmentByChild.contains(childEntityValue) || uniformScale <= 0.0f)
        {
            zenith->Push(false);
            return 1;
        }

        PACT::PactStorage* pactStorage = ServiceLocator::GetPactStorage();
        std::string skeletonPath;
        FileFormat::AssetID skeletonAssetID = FileFormat::INVALID_ASSET_ID;
        if (!pactStorage || !ResolveModelAssetPath(skeletonPathRaw, pactStorage, skeletonPath, skeletonAssetID))
        {
            zenith->Push(false);
            return 1;
        }
        AnimationLoading::AnimationAssetRegistry& assets = gameRenderer->GetRenderAssetResources()->GetAnimationRegistry();
        const AnimationLoading::SkeletonHandle skeleton = assets.LoadSkeleton(skeletonAssetID);
        AssetLoading::Diagnostic skeletonDiagnostic;
        const AnimationLoading::SkeletonAssetView* skeletonView = assets.Get(skeleton, &skeletonDiagnostic);
        if (!skeletonView)
        {
            assets.Release(skeleton);
            zenith->Push(false);
            return 1;
        }
        const u64 semanticID = Animation::HashAnimationSemantic("skeleton-attachment", semanticName);
        const Animation::PoseAttachmentHandle attachment = deformation->CreateAttachment(parentBinding->second->poseSource, *skeletonView, semanticID, true, vec3(2.0f));
        assets.Release(skeleton);
        if (!attachment)
        {
            zenith->Push(false);
            return 1;
        }

        const mat4x4 translationMatrix = glm::translate(mat4x4(1.0f), translation);
        const mat4x4 rotationMatrix = glm::eulerAngleXYZ(glm::radians(rotationDegrees.x), glm::radians(rotationDegrees.y), glm::radians(rotationDegrees.z));
        const mat4x4 scaleMatrix = glm::scale(mat4x4(1.0f), vec3(uniformScale));
        const mat4x4 childLocalTransform = translationMatrix * rotationMatrix * scaleMatrix;
        if (!deformation->BindModelAttachment(attachment, parentScene, bridge->Get(parentEntity), bridge->Get(childEntity), childLocalTransform))
        {
            deformation->DestroyAttachment(attachment);
            zenith->Push(false);
            return 1;
        }
        s_animationAttachmentByChild[childEntityValue] = {parentEntityValue, childEntityValue, attachment, childLocalTransform};
        bridge->SetShadowDynamic(childEntity, true);
        NC_LOG_INFO("ANIMATION_ATTACHMENT bound parent={} child={} skeleton={} semantic={}", parentEntityValue, childEntityValue, skeletonPath, semanticName);
        zenith->Push(true);
        return 1;
    }

    i32 AssetHandler::SetModelAttachmentTransform(Zenith* zenith)
    {
        const u32 childEntityValue = zenith->CheckVal<u32>(1);
        const auto found = s_animationAttachmentByChild.find(childEntityValue);
        if (found == s_animationAttachmentByChild.end())
        {
            zenith->Push(false);
            return 1;
        }
        const vec3 translation(zenith->CheckVal<f32>(2), zenith->CheckVal<f32>(3), zenith->CheckVal<f32>(4));
        const vec3 rotationDegrees(zenith->CheckVal<f32>(5), zenith->CheckVal<f32>(6), zenith->CheckVal<f32>(7));
        const f32 uniformScale = zenith->CheckVal<f32>(8);
        if (!std::isfinite(uniformScale) || uniformScale <= 0.0f)
        {
            zenith->Push(false);
            return 1;
        }
        GameRenderer* gameRenderer = ServiceLocator::GetGameRenderer();
        ModelScene::ModelSceneBridge* bridge = gameRenderer ? gameRenderer->GetModelSceneBridge() : nullptr;
        RenderScenes::RenderScene* scene = bridge ? bridge->GetScene(static_cast<entt::entity>(found->second.parentEntity)) : nullptr;
        Animation::ModelDeformationSystem* deformation = scene ? scene->GetModelDeformationSystem() : nullptr;
        const mat4x4 childLocalTransform = glm::translate(mat4x4(1.0f), translation) * glm::eulerAngleXYZ(glm::radians(rotationDegrees.x), glm::radians(rotationDegrees.y), glm::radians(rotationDegrees.z)) * glm::scale(mat4x4(1.0f), vec3(uniformScale));
        if (!deformation || !deformation->SetModelAttachmentLocalTransform(found->second.attachment, childLocalTransform))
        {
            zenith->Push(false);
            return 1;
        }
        found->second.childLocalTransform = childLocalTransform;
        zenith->Push(true);
        return 1;
    }

    i32 AssetHandler::GetModelAttachmentPoint(Zenith* zenith)
    {
        const u32 childEntityValue = zenith->CheckVal<u32>(1);
        const auto found = s_animationAttachmentByChild.find(childEntityValue);
        if (found == s_animationAttachmentByChild.end())
        {
            zenith->Push();
            return 1;
        }
        GameRenderer* gameRenderer = ServiceLocator::GetGameRenderer();
        ModelScene::ModelSceneBridge* bridge = gameRenderer ? gameRenderer->GetModelSceneBridge() : nullptr;
        RenderScenes::RenderScene* scene = bridge ? bridge->GetScene(static_cast<entt::entity>(found->second.parentEntity)) : nullptr;
        Animation::ModelDeformationSystem* deformation = scene ? scene->GetModelDeformationSystem() : nullptr;
        const Animation::PoseAttachment* attachment = deformation ? deformation->GetAttachment(found->second.attachment) : nullptr;
        if (!attachment || !attachment->valid)
        {
            zenith->Push();
            return 1;
        }
        const vec3 localPoint(
            zenith->IsNumber(2) ? zenith->Get<f32>(2) : 0.0f,
            zenith->IsNumber(3) ? zenith->Get<f32>(3) : 0.0f,
            zenith->IsNumber(4) ? zenith->Get<f32>(4) : 0.0f);
        const vec4 modelPoint = mat4x4(attachment->currentTransform) * found->second.childLocalTransform * vec4(localPoint, 1.0f);
        zenith->Push(vec3(modelPoint));
        return 1;
    }

    i32 AssetHandler::GetModelAttachmentLocalPoint(Zenith* zenith)
    {
        const u32 childEntityValue = zenith->CheckVal<u32>(1);
        const auto found = s_animationAttachmentByChild.find(childEntityValue);
        if (found == s_animationAttachmentByChild.end())
        {
            zenith->Push();
            return 1;
        }
        const vec3 localPoint(
            zenith->IsNumber(2) ? zenith->Get<f32>(2) : 0.0f,
            zenith->IsNumber(3) ? zenith->Get<f32>(3) : 0.0f,
            zenith->IsNumber(4) ? zenith->Get<f32>(4) : 0.0f);
        zenith->Push(vec3(found->second.childLocalTransform * vec4(localPoint, 1.0f)));
        return 1;
    }

    i32 AssetHandler::GetModelAttachmentGripDiagnostics(Zenith* zenith)
    {
        const u32 childEntityValue = zenith->CheckVal<u32>(1);
        const auto found = s_animationAttachmentByChild.find(childEntityValue);
        if (found == s_animationAttachmentByChild.end())
        {
            zenith->Push();
            return 1;
        }
        const vec3 primaryLocal(zenith->CheckVal<f32>(2), zenith->CheckVal<f32>(3), zenith->CheckVal<f32>(4));
        const vec3 secondaryLocal(zenith->CheckVal<f32>(5), zenith->CheckVal<f32>(6), zenith->CheckVal<f32>(7));
        const vec3 handleAxisLocal(zenith->CheckVal<f32>(8), zenith->CheckVal<f32>(9), zenith->CheckVal<f32>(10));
        const vec3 bladeEdgeLocal(zenith->CheckVal<f32>(11), zenith->CheckVal<f32>(12), zenith->CheckVal<f32>(13));
        const vec3 rightGripAxisLocal(zenith->CheckVal<f32>(14), zenith->CheckVal<f32>(15), zenith->CheckVal<f32>(16));
        const bool draw = zenith->IsBoolean(17) && zenith->ToBoolean(17);
        const f32 axisLength = zenith->IsNumber(18) ? std::max(zenith->Get<f32>(18), 0.01f) : 0.12f;
        const f32 positionTolerance = zenith->IsNumber(19) ? std::max(zenith->Get<f32>(19), 0.0f) : 0.015f;
        const f32 angleToleranceDegrees = zenith->IsNumber(20) ? std::max(zenith->Get<f32>(20), 0.0f) : 8.0f;
        if (glm::length2(handleAxisLocal) <= 1.0e-8f || glm::length2(bladeEdgeLocal) <= 1.0e-8f || glm::length2(rightGripAxisLocal) <= 1.0e-8f)
        {
            zenith->Push();
            return 1;
        }

        GameRenderer* gameRenderer = ServiceLocator::GetGameRenderer();
        ModelScene::ModelSceneBridge* bridge = gameRenderer ? gameRenderer->GetModelSceneBridge() : nullptr;
        RenderScenes::RenderScene* scene = bridge ? bridge->GetScene(static_cast<entt::entity>(found->second.parentEntity)) : nullptr;
        Animation::ModelDeformationSystem* deformation = scene ? scene->GetModelDeformationSystem() : nullptr;
        const Animation::PoseAttachment* attachment = deformation ? deformation->GetAttachment(found->second.attachment) : nullptr;
        const auto parentBinding = s_animationBindingByEntity.find(found->second.parentEntity);
        const Animation::PoseSource* pose = deformation && parentBinding != s_animationBindingByEntity.end() ? deformation->GetPoseSource(parentBinding->second->poseSource) : nullptr;
        const ModelScene::ModelInstanceGPURecord* parentInstance = scene && bridge ? scene->GetModelInstance(bridge->Get(static_cast<entt::entity>(found->second.parentEntity))) : nullptr;
        if (!attachment || !attachment->valid || !pose || !parentInstance || !gameRenderer)
        {
            zenith->Push();
            return 1;
        }
        AnimationLoading::AnimationAssetRegistry& assets = gameRenderer->GetRenderAssetResources()->GetAnimationRegistry();
        AssetLoading::Diagnostic diagnostic;
        const AnimationLoading::SkeletonAssetView* skeleton = assets.Get(pose->skeleton, &diagnostic);
        if (!skeleton)
        {
            zenith->Push();
            return 1;
        }
        const u64 leftSemanticID = Animation::HashAnimationSemantic("skeleton-attachment", "left_hand");
        const auto leftAttachment = std::find_if(skeleton->attachments.begin(), skeleton->attachments.end(), [leftSemanticID](const FileFormat::Animation::SkeletonAttachment& value) { return value.semanticID == leftSemanticID; });
        if (leftAttachment == skeleton->attachments.end() || leftAttachment->jointIndex >= pose->modelTransforms.size())
        {
            zenith->Push();
            return 1;
        }

        const mat4x4 primarySocketModel = mat4x4(attachment->currentTransform);
        const mat4x4 secondarySocketModel = mat4x4(Math::AffineMatrix::MatrixMul(pose->modelTransforms[leftAttachment->jointIndex], Animation::ComposeLocalTransform(leftAttachment->localTransform)));
        const mat4x4 weaponModel = primarySocketModel * found->second.childLocalTransform;
        const vec3 primarySocketPosition(primarySocketModel[3]);
        const vec3 secondarySocketPosition(secondarySocketModel[3]);
        const vec3 primaryTargetPosition(weaponModel * vec4(primaryLocal, 1.0f));
        const vec3 secondaryTargetPosition(weaponModel * vec4(secondaryLocal, 1.0f));
        const vec3 handleAxis = glm::normalize(vec3(weaponModel * vec4(glm::normalize(handleAxisLocal), 0.0f)));
        const vec3 bladeEdgeAxis = glm::normalize(vec3(weaponModel * vec4(glm::normalize(bladeEdgeLocal), 0.0f)));
        const vec3 rightGripAxis = glm::normalize(vec3(primarySocketModel * vec4(glm::normalize(rightGripAxisLocal), 0.0f)));
        const vec3 handSpanVector = secondarySocketPosition - primarySocketPosition;
        const vec3 handSpanAxis = glm::length2(handSpanVector) > 1.0e-8f ? glm::normalize(handSpanVector) : vec3(0.0f);
        const auto parallelAngleDegrees = [](const vec3& first, const vec3& second) { return glm::degrees(std::acos(std::clamp(std::abs(glm::dot(first, second)), 0.0f, 1.0f))); };
        const f32 handleHandSpanDegrees = glm::length2(handSpanAxis) > 0.0f ? parallelAngleDegrees(handleAxis, handSpanAxis) : 180.0f;
        const f32 primaryPositionError = glm::length(primaryTargetPosition - primarySocketPosition);
        const f32 secondaryPositionError = glm::length(secondaryTargetPosition - secondarySocketPosition);
        const f32 bladeHandleDegrees = parallelAngleDegrees(handleAxis, bladeEdgeAxis);
        const f32 rightGripHandleDegrees = parallelAngleDegrees(handleAxis, rightGripAxis);

        const auto closestAxis = [&parallelAngleDegrees, &handleAxis](const mat4x4& transform, f32& angle) {
            i32 result = 1;
            angle = 180.0f;
            for (i32 axis = 0; axis < 3; ++axis)
            {
                const vec3 candidate = glm::normalize(vec3(transform[axis]));
                const f32 candidateAngle = parallelAngleDegrees(candidate, handleAxis);
                if (candidateAngle < angle)
                {
                    angle = candidateAngle;
                    result = axis + 1;
                }
            }
            return result;
        };
        f32 primaryClosestAxisDegrees = 0.0f;
        f32 secondaryClosestAxisDegrees = 0.0f;
        const i32 primaryClosestAxis = closestAxis(primarySocketModel, primaryClosestAxisDegrees);
        const i32 secondaryClosestAxis = closestAxis(secondarySocketModel, secondaryClosestAxisDegrees);
        const bool passed = primaryPositionError <= positionTolerance && secondaryPositionError <= positionTolerance && handleHandSpanDegrees <= angleToleranceDegrees && rightGripHandleDegrees <= angleToleranceDegrees && secondaryClosestAxisDegrees <= angleToleranceDegrees;

        if (draw)
        {
            DebugRenderer* debugRenderer = gameRenderer->GetDebugRenderer();
            const mat4x4 parentWorld = parentInstance->currentWorld;
            const auto worldPoint = [&parentWorld](const vec3& point) { return vec3(parentWorld * vec4(point, 1.0f)); };
            const auto drawFrame = [debugRenderer, &parentWorld, axisLength](const mat4x4& modelTransform, f32 scale) {
                const mat4x4 worldTransform = parentWorld * modelTransform;
                const vec3 origin(worldTransform[3]);
                debugRenderer->DrawLine3D(origin, origin + glm::normalize(vec3(worldTransform[0])) * axisLength * scale, Color::Red);
                debugRenderer->DrawLine3D(origin, origin + glm::normalize(vec3(worldTransform[1])) * axisLength * scale, Color::Green);
                debugRenderer->DrawLine3D(origin, origin + glm::normalize(vec3(worldTransform[2])) * axisLength * scale, Color::Blue);
            };
            drawFrame(primarySocketModel, 1.0f);
            drawFrame(secondarySocketModel, 1.0f);
            const mat4x4 primaryTargetFrame = glm::translate(weaponModel, primaryLocal);
            const mat4x4 secondaryTargetFrame = glm::translate(weaponModel, secondaryLocal);
            drawFrame(primaryTargetFrame, 0.65f);
            drawFrame(secondaryTargetFrame, 0.65f);
            const vec3 primaryWorld = worldPoint(primarySocketPosition);
            const vec3 secondaryWorld = worldPoint(secondarySocketPosition);
            const vec3 primaryTargetWorld = worldPoint(primaryTargetPosition);
            const vec3 secondaryTargetWorld = worldPoint(secondaryTargetPosition);
            const vec3 handleWorld = glm::normalize(vec3(parentWorld * vec4(handleAxis, 0.0f)));
            const vec3 edgeWorld = glm::normalize(vec3(parentWorld * vec4(bladeEdgeAxis, 0.0f)));
            const vec3 rightGripWorld = glm::normalize(vec3(parentWorld * vec4(rightGripAxis, 0.0f)));
            const f32 spanLength = std::max(glm::length(handSpanVector), axisLength * 2.0f);
            debugRenderer->DrawLine3D(primaryWorld - handleWorld * spanLength, primaryWorld + handleWorld * spanLength * 1.5f, Color::Magenta);
            debugRenderer->DrawLine3D(primaryWorld - rightGripWorld * spanLength, primaryWorld + rightGripWorld * spanLength, Color::Yellow);
            const vec3 arrowReference = std::abs(glm::dot(rightGripWorld, vec3(0.0f, 1.0f, 0.0f))) < 0.9f ? vec3(0.0f, 1.0f, 0.0f) : vec3(1.0f, 0.0f, 0.0f);
            const vec3 arrowSide = glm::normalize(glm::cross(rightGripWorld, arrowReference));
            const vec3 arrowUp = glm::normalize(glm::cross(rightGripWorld, arrowSide));
            const vec3 arrowTip = primaryWorld + rightGripWorld * spanLength;
            const vec3 arrowBase = arrowTip - rightGripWorld * axisLength * 0.35f;
            const f32 arrowWidth = axisLength * 0.16f;
            debugRenderer->DrawLine3D(arrowTip, arrowBase + arrowSide * arrowWidth, Color::Yellow);
            debugRenderer->DrawLine3D(arrowTip, arrowBase - arrowSide * arrowWidth, Color::Yellow);
            debugRenderer->DrawLine3D(arrowTip, arrowBase + arrowUp * arrowWidth, Color::Yellow);
            debugRenderer->DrawLine3D(arrowTip, arrowBase - arrowUp * arrowWidth, Color::Yellow);
            debugRenderer->DrawLine3D(primaryWorld, secondaryWorld, Color::Cyan);
            debugRenderer->DrawLine3D(primaryTargetWorld, secondaryTargetWorld, Color::PastelOrange);
            debugRenderer->DrawLine3D(primaryWorld, primaryTargetWorld, primaryPositionError <= positionTolerance ? Color::Green : Color::Red);
            debugRenderer->DrawLine3D(secondaryWorld, secondaryTargetWorld, secondaryPositionError <= positionTolerance ? Color::Green : Color::Red);
            debugRenderer->DrawLine3D(primaryWorld, primaryWorld + edgeWorld * spanLength, Color::White);
            debugRenderer->DrawSphere3D(primaryWorld, axisLength * 0.08f, 12, Color::Cyan);
            debugRenderer->DrawSphere3D(secondaryWorld, axisLength * 0.08f, 12, Color::Cyan);
            debugRenderer->DrawSphere3D(primaryTargetWorld, axisLength * 0.06f, 12, Color::PastelOrange);
            debugRenderer->DrawSphere3D(secondaryTargetWorld, axisLength * 0.06f, 12, Color::PastelOrange);
        }

        zenith->CreateTable();
        zenith->AddTableField("passed", passed);
        zenith->AddTableField("primaryPositionError", primaryPositionError);
        zenith->AddTableField("secondaryPositionError", secondaryPositionError);
        zenith->AddTableField("handleHandSpanDegrees", handleHandSpanDegrees);
        zenith->AddTableField("bladeHandleDegrees", bladeHandleDegrees);
        zenith->AddTableField("rightGripHandleDegrees", rightGripHandleDegrees);
        zenith->AddTableField("primaryClosestAxis", primaryClosestAxis);
        zenith->AddTableField("primaryClosestAxisDegrees", primaryClosestAxisDegrees);
        zenith->AddTableField("secondaryClosestAxis", secondaryClosestAxis);
        zenith->AddTableField("secondaryClosestAxisDegrees", secondaryClosestAxisDegrees);
        zenith->AddTableField("primarySocketPosition", primarySocketPosition);
        zenith->AddTableField("secondarySocketPosition", secondarySocketPosition);
        zenith->AddTableField("primaryTargetPosition", primaryTargetPosition);
        zenith->AddTableField("secondaryTargetPosition", secondaryTargetPosition);
        zenith->AddTableField("handleAxis", handleAxis);
        zenith->AddTableField("bladeEdgeAxis", bladeEdgeAxis);
        zenith->AddTableField("rightGripAxis", rightGripAxis);
        zenith->AddTableField("handSpanAxis", handSpanAxis);
        if (pose->debugIKOperationCount != 0)
        {
            const Animation::PoseIKDebugOperation& operation = pose->debugIKOperations[0];
            zenith->AddTableField("ikFinalError", operation.finalError);
            zenith->AddTableField("ikStatus", static_cast<u32>(operation.status));
            zenith->AddTableField("ikClamped", operation.clamped);
            zenith->AddTableField("ikJointLimitClamped", operation.jointLimitClamped);
        }
        return 1;
    }

    i32 AssetHandler::GetAnimationAttachmentPoint(Zenith* zenith)
    {
        std::shared_ptr<AnimationPreviewBinding> binding;
        Animation::ModelDeformationSystem* deformation = GetControllerBinding(zenith->CheckVal<u32>(1), binding);
        const char* semanticName = zenith->CheckVal<const char*>(2);
        const Animation::PoseSource* pose = deformation && binding ? deformation->GetPoseSource(binding->poseSource) : nullptr;
        GameRenderer* gameRenderer = ServiceLocator::GetGameRenderer();
        if (!pose || !gameRenderer || !semanticName)
        {
            zenith->Push();
            return 1;
        }
        AnimationLoading::AnimationAssetRegistry& assets = gameRenderer->GetRenderAssetResources()->GetAnimationRegistry();
        AssetLoading::Diagnostic diagnostic;
        const AnimationLoading::SkeletonAssetView* skeleton = assets.Get(pose->skeleton, &diagnostic);
        if (!skeleton)
        {
            zenith->Push();
            return 1;
        }
        const u64 semanticID = Animation::HashAnimationSemantic("skeleton-attachment", semanticName);
        const auto attachment = std::find_if(skeleton->attachments.begin(), skeleton->attachments.end(), [semanticID](const FileFormat::Animation::SkeletonAttachment& value) { return value.semanticID == semanticID; });
        if (attachment == skeleton->attachments.end() || attachment->jointIndex >= pose->modelTransforms.size())
        {
            zenith->Push();
            return 1;
        }
        const bool jointOrigin = zenith->IsBoolean(3) && zenith->ToBoolean(3);
        if (jointOrigin)
        {
            zenith->Push(vec3(pose->modelTransforms[attachment->jointIndex][3]));
            return 1;
        }
        const mat4a socketTransform = Math::AffineMatrix::MatrixMul(pose->modelTransforms[attachment->jointIndex], Animation::ComposeLocalTransform(attachment->localTransform));
        zenith->Push(vec3(socketTransform[3]));
        return 1;
    }

    i32 AssetHandler::GetAnimationControllerState(Zenith* zenith)
    {
        std::shared_ptr<AnimationPreviewBinding> binding;
        Animation::ModelDeformationSystem* deformation = GetControllerBinding(zenith->CheckVal<u32>(1), binding);
        Animation::AnimationControllerState state;
        if (!deformation || !deformation->GetControllerState(binding->controller, state))
        {
            zenith->Push();
            return 1;
        }
        zenith->CreateTable();
        RefreshGraphPublication(*deformation, *binding);
        zenith->AddTableField("graphRevision", binding->publicationRevision);
        zenith->AddTableField("currentState", state.currentState);
        zenith->AddTableField("transitionTarget", state.transitionTarget);
        zenith->AddTableField("stateTimeUS", state.stateTimeMicroseconds);
        zenith->AddTableField("transitionTimeUS", state.transitionTimeMicroseconds);
        zenith->AddTableField("transitionDurationUS", state.transitionDurationMicroseconds);
        zenith->AddTableField("transitioning", state.transitioning);
        zenith->AddTableField("hasContinuitySnapshot", state.hasContinuitySnapshot);
        zenith->AddTableField("activeBlendSamples", state.activeBlendSamples);
        zenith->AddTableField("activeBlendTriangle", state.activeBlendTriangle);
        zenith->AddTableField("blendWeight0", state.activeBlendWeights[0]);
        zenith->AddTableField("blendWeight1", state.activeBlendWeights[1]);
        zenith->AddTableField("blendWeight2", state.activeBlendWeights[2]);
        zenith->AddTableField("playbackRate0", state.synchronizationPlaybackRates[0]);
        zenith->AddTableField("playbackRate1", state.synchronizationPlaybackRates[1]);
        zenith->AddTableField("playbackRate2", state.synchronizationPlaybackRates[2]);
        zenith->AddTableField("synchronizationLeader", state.synchronizationLeader);
        zenith->AddTableField("previousSynchronizationMarker", state.previousSynchronizationMarker);
        zenith->AddTableField("nextSynchronizationMarker", state.nextSynchronizationMarker);
        zenith->AddTableField("requestedSlotSemanticID", state.requestedSlotSemanticID);
        zenith->AddTableField("resolvedSlotSemanticID", state.resolvedSlotSemanticID);
        zenith->AddTableField("resolvedClipAssetID", state.resolvedClipAssetID);
        zenith->AddTableField("resolvedVariationID", state.resolvedVariationID);
        zenith->AddTableField("resolvedFallbackDepth", state.resolvedFallbackDepth);
        zenith->AddTableField("slotResolution", static_cast<u32>(state.slotResolution));
        zenith->AddTableField("synchronizationFallback", state.synchronizationFallback);
        zenith->AddTableField("activeLayers", state.activeLayers);
        zenith->AddTableField("activeIKOperations", state.activeIKOperations);
        zenith->CreateTable();
        if (const auto* pose = deformation->GetPoseSource(binding->poseSource))
            for (u32 index = 0; index < pose->debugIKOperationCount; ++index)
            {
                const auto& operation = pose->debugIKOperations[index];
                zenith->CreateTable();
                zenith->AddTableField("finalError", operation.finalError);
                zenith->AddTableField("clamped", operation.clamped);
                zenith->AddTableField("jointLimitClamped", operation.jointLimitClamped);
                zenith->AddTableField("goalX", operation.goalModel.x);
                zenith->AddTableField("goalY", operation.goalModel.y);
                zenith->AddTableField("goalZ", operation.goalModel.z);
                zenith->SetTableKey(index + 1);
            }
        zenith->SetTableKey("ikDiagnostics");
        zenith->AddTableField("activeActionSemanticID", state.activeActionSemanticID);
        zenith->AddTableField("activeActionTimeUS", state.activeActionTimeMicroseconds);
        zenith->AddTableField("activeActionStatus", static_cast<u32>(state.activeActionStatus));
        std::vector<vec3> parameterValues;
        if (deformation->GetControllerParameterValues(binding->controller, parameterValues) && binding->graphSource)
        {
            zenith->CreateTable();
            const u32 parameterCount = std::min(static_cast<u32>(parameterValues.size()), static_cast<u32>(binding->graphSource->parameters.size()));
            for (u32 parameterIndex = 0; parameterIndex < parameterCount; ++parameterIndex)
            {
                const Animation::EditableGraphParameter& parameter = binding->graphSource->parameters[parameterIndex];
                const vec3& value = parameterValues[parameterIndex];
                if (parameter.type == FileFormat::Animation::GraphValueType::Float3)
                {
                    zenith->CreateTable();
                    zenith->AddTableField("x", value.x);
                    zenith->AddTableField("y", value.y);
                    zenith->AddTableField("z", value.z);
                    zenith->SetTableKey(parameter.name.c_str());
                }
                else
                    zenith->AddTableField(parameter.name.c_str(), value.x);
            }
            zenith->SetTableKey("parameters");
        }
        return 1;
    }

    static void RefreshGraphPublication(Animation::ModelDeformationSystem& deformation, AnimationPreviewBinding& binding)
    {
        if (!binding.pendingCompiled)
            return;
        u64 hash = 0;
        bool pending = false;
        Animation::AnimationGraphMigration migration;
        if (!deformation.GetControllerGraphPublication(binding.controller, hash, pending, migration) || pending || hash != binding.pendingCompiled->contentHash)
            return;
        binding.graphSource = std::move(binding.pendingSource);
        binding.compiledGraph = std::move(binding.pendingCompiled);
        ++binding.publicationRevision;
        binding.authoringStatus = std::string("Published; state ") + (migration.statePreserved ? "preserved" : "reset") + ", parameters " + std::to_string(migration.parametersPreserved) + ", actions interrupted " + std::to_string(migration.actionsInterrupted);
    }

    static Animation::AnimationGraphDocument* GetGraphDocument(Animation::ModelDeformationSystem* deformation, const std::shared_ptr<AnimationPreviewBinding>& binding)
    {
        if (!deformation || !binding || !binding->graphSource)
            return nullptr;
        RefreshGraphPublication(*deformation, *binding);
        if (!binding->document)
            binding->document = std::make_unique<Animation::AnimationGraphDocument>(*binding->graphSource);
        return binding->document.get();
    }

    static const std::unordered_map<u64, std::string>& AuthoringJointNames()
    {
        static const auto names = []
        {
            std::unordered_map<u64, std::string> result;
            std::ifstream stream("../spec/Animation/BipedalHumanoid.v1.json");
            const auto dictionary = nlohmann::json::parse(stream, nullptr, false);
            if (dictionary.is_object() && dictionary.contains("joints"))
                for (const auto& joint : dictionary["joints"])
                {
                    const auto name = joint.value("semantic", std::string());
                    result[Animation::HashAnimationSemantic("humanoid", name)] = name;
                    result[Animation::HashAnimationSemantic("source-joint", name)] = name;
                    result[Animation::HashAnimationSemantic("source-joint", "mixamorig:" + name)] = name;
                    result[Animation::HashAnimationSemantic("source-joint", "mixamorig1:" + name)] = name;
                }
            return result;
        }();
        return names;
    }

    static std::vector<FileFormat::Animation::SkeletonJoint> AuthoringHierarchy(const Animation::AnimationAssetDocument& document, const Animation::PoseSource* pose, AnimationLoading::AnimationAssetRegistry& assets)
    {
        using Type = Animation::AnimationDocumentType;
        std::vector<FileFormat::Animation::SkeletonJoint> joints;
        if (document.GetType() == Type::Skeleton)
        {
            const auto view = AnimationLoading::AnimationAssetReader::ReadSkeleton(document.GetPayload());
            if (view) joints.assign(view.view.joints.begin(), view.view.joints.end());
            return joints;
        }
        if (document.GetType() == Type::RigFamily)
        {
            const auto view = AnimationLoading::AnimationAssetReader::ReadRigFamily(document.GetPayload());
            if (view) for (const auto& joint : view.view.joints)
            {
                FileFormat::Animation::SkeletonJoint value;
                value.semanticID = joint.semanticID; value.parentJointIndex = joint.parentJointIndex; value.restTransform = joint.canonicalTransform;
                joints.push_back(value);
            }
            return joints;
        }
        auto skeleton = pose ? assets.Lease(pose->skeleton) : nullptr;
        if (document.GetType() == Type::Mask)
        {
            const auto mask = AnimationLoading::AnimationAssetReader::ReadBoneMask(document.GetPayload());
            if (!mask) return joints;
            if (mask.view.root.poseDomainType == FileFormat::Animation::PoseDomainType::RigFamily)
            {
                const auto handle = assets.LoadRigFamily(mask.view.root.poseDomainAssetID);
                const auto family = assets.Lease(handle);
                if (family) for (const auto& joint : family->joints)
                {
                    FileFormat::Animation::SkeletonJoint value;
                    value.semanticID = joint.semanticID; value.parentJointIndex = joint.parentJointIndex;
                    value.restTransform = joint.canonicalTransform;
                    joints.push_back(value);
                }
                assets.Release(handle);
                return joints;
            }
            const auto handle = assets.LoadSkeleton(mask.view.root.poseDomainAssetID);
            skeleton = assets.Lease(handle);
            assets.Release(handle);
        }
        if (skeleton) joints.assign(skeleton->joints.begin(), skeleton->joints.end());
        return joints;
    }

    i32 AssetHandler::GetAnimationJointScreenPositions(Zenith* zenith)
    {
        const auto entity = static_cast<entt::entity>(zenith->CheckVal<u32>(1));
        std::shared_ptr<AnimationPreviewBinding> binding;
        auto* deformation = GetControllerBinding(static_cast<u32>(entity), binding, false);
        auto* renderer = ServiceLocator::GetGameRenderer();
        auto* bridge = renderer ? renderer->GetModelSceneBridge() : nullptr;
        auto* scene = bridge ? bridge->GetScene(entity) : nullptr;
        const auto* instance = scene ? scene->GetModelInstance(bridge->Get(entity)) : nullptr;
        const auto* pose = deformation ? deformation->GetPoseSource(binding->poseSource) : nullptr;
        auto* registries = ServiceLocator::GetEnttRegistries();
        auto* registry = registries ? registries->gameRegistry : nullptr;
        const auto* active = registry ? registry->ctx().find<ECS::Singletons::ActiveCamera>() : nullptr;
        const auto* camera = active ? registry->try_get<ECS::Components::Camera>(active->entity) : nullptr;
        zenith->CreateTable();
        if (!pose || !instance || !camera || !binding->assetDocument) return 1;
        auto& assets = renderer->GetRenderAssetResources()->GetAnimationRegistry();
        const auto skeleton = assets.Lease(pose->skeleton);
        if (!skeleton) return 1;
        const auto hierarchy = AuthoringHierarchy(*binding->assetDocument, pose, assets);
        bool familyDomain = binding->assetDocument->GetType() == Animation::AnimationDocumentType::RigFamily;
        if (binding->assetDocument->GetType() == Animation::AnimationDocumentType::Mask)
        {
            const auto mask = AnimationLoading::AnimationAssetReader::ReadBoneMask(binding->assetDocument->GetPayload());
            familyDomain = mask && mask.view.root.poseDomainType == FileFormat::Animation::PoseDomainType::RigFamily;
        }
        const mat4x4 modelToClip = camera->worldToClip * instance->currentWorld;
        u32 output = 1;
        for (u32 index = 0; index < hierarchy.size(); ++index)
        {
            for (u32 concrete = 0; concrete < skeleton->joints.size() && concrete < pose->modelTransforms.size(); ++concrete)
            {
                const auto& joint = skeleton->joints[concrete];
                if (familyDomain ? joint.familyJointIndex != static_cast<i32>(index) : joint.semanticID != hierarchy[index].semanticID) continue;
                const vec4 clip = modelToClip * vec4(vec3(pose->modelTransforms[concrete][3]), 1.0f);
                if (!std::isfinite(clip.w) || clip.w <= 0.0f) break;
                const vec3 ndc = vec3(clip) / clip.w;
                if (!std::isfinite(ndc.x) || !std::isfinite(ndc.y) || ndc.z < 0.0f || ndc.z > 1.0f) break;
                zenith->CreateTable();
                zenith->AddTableField("index", index);
                zenith->AddTableField("concreteIndex", concrete);
                zenith->AddTableField("x", ndc.x * 0.5f + 0.5f);
                zenith->AddTableField("y", ndc.y * 0.5f + 0.5f);
                zenith->SetTableKey(output++);
                break;
            }
        }
        return 1;
    }

    i32 AssetHandler::AnimationDump(Zenith* zenith)
    {
        std::shared_ptr<AnimationPreviewBinding> binding;
        auto* deformation = GetControllerBinding(zenith->CheckVal<u32>(1), binding, false);
        const std::string name = zenith->CheckVal<const char*>(2);
        std::string error;
        bool success = false;
        if (!deformation || name.empty() || name.size() > 64 || !std::all_of(name.begin(), name.end(), [](unsigned char c) { return std::isalnum(c) || c == '-' || c == '_'; })) error = "Choose a bound preview and a simple capture name";
        else try
        {
            Animation::PoseRecipe recipe;
            RefreshGraphPublication(*deformation, *binding);
            const bool hasRecipe = binding->controller && deformation->CaptureControllerRecipe(binding->controller, recipe);
            const auto* pose = deformation->GetPoseSource(binding->poseSource);
            if (!pose) throw std::runtime_error("Preview pose is unavailable");
            nlohmann::json dump;
            dump["poseGeneration"] = pose->poseGeneration;
            dump["motionGeneration"] = pose->motionGeneration;
            dump["sampleTimeMicroseconds"] = pose->sampleTimeMicroseconds;
            dump["localPose"] = nlohmann::json::array();
            dump["ik"] = nlohmann::json::array();
            for (u32 index = 0; index < pose->debugIKOperationCount; ++index)
            {
                const auto& operation = pose->debugIKOperations[index];
                dump["ik"].push_back({{"goal", {operation.goalModel.x, operation.goalModel.y, operation.goalModel.z}}, {"pole", {operation.poleModel.x, operation.poleModel.y, operation.poleModel.z}}, {"finalError", operation.finalError}, {"clamped", operation.clamped}, {"jointLimitClamped", operation.jointLimitClamped}});
            }
            if (hasRecipe)
            {
                auto sampleJSON = [](const Animation::PoseRecipeSample& sample) { return nlohmann::json{{"clipAssetID", sample.clipAssetID}, {"timeMicroseconds", sample.timeMicroseconds}, {"weight", sample.weight}, {"variation", sample.stableVariationID}}; };
                auto& output = dump["recipe"];
                output = {{"sourceState", recipe.sourceState}, {"targetState", recipe.targetState}, {"transitionWeight", recipe.transitionWeight}, {"usesSnapshot", recipe.usesSnapshot}, {"samples", nlohmann::json::array()}, {"layers", nlohmann::json::array()}, {"ik", nlohmann::json::array()}};
                for (u32 index = 0; index < recipe.sampleCount; ++index) output["samples"].push_back(sampleJSON(recipe.samples[index]));
                for (u32 index = 0; index < recipe.layerCount; ++index)
                {
                    const auto& layer = recipe.layers[index];
                    output["layers"].push_back({{"sample", sampleJSON(layer.sample)}, {"maskAssetID", layer.maskAssetID}, {"weight", layer.weight}, {"type", static_cast<u32>(layer.type)}, {"order", layer.executionOrder}, {"composeInSourceDomain", layer.composeInSourceDomain}});
                }
                for (u32 index = 0; index < recipe.ikOperationCount; ++index)
                {
                    const auto& operation = recipe.ikOperations[index];
                    output["ik"].push_back({{"rigAssetID", operation.rigAssetID}, {"solver", operation.solverSemanticID}, {"goal", {operation.goalModel.x, operation.goalModel.y, operation.goalModel.z}}, {"weight", operation.weight}, {"order", operation.executionOrder}, {"goalAttachment", operation.goalAttachmentSemanticID}, {"effectorAttachment", operation.effectorAttachmentSemanticID}, {"alignmentAxis", {operation.effectorAlignmentAxis.x, operation.effectorAlignmentAxis.y, operation.effectorAlignmentAxis.z}}});
                }
            }
            for (const auto& joint : pose->localPose)
                dump["localPose"].push_back({{"translation", {joint.translation.x, joint.translation.y, joint.translation.z}}, {"rotationWXYZ", {joint.rotation.w, joint.rotation.x, joint.rotation.y, joint.rotation.z}}, {"scale", {joint.scale.x, joint.scale.y, joint.scale.z}}});
            Animation::AnimationControllerState state;
            if (binding->controller && deformation->GetControllerState(binding->controller, state))
            {
                dump["controller"] = {{"state", state.currentState}, {"stateTimeMicroseconds", state.stateTimeMicroseconds}, {"transitionTarget", state.transitionTarget}, {"transitionTimeMicroseconds", state.transitionTimeMicroseconds}, {"transitioning", state.transitioning}, {"resolvedClipAssetID", state.resolvedClipAssetID}, {"variation", state.resolvedVariationID}, {"fallbackDepth", state.resolvedFallbackDepth}, {"blendWeights", state.activeBlendWeights}, {"playbackRates", state.synchronizationPlaybackRates}};
                if (binding->graphSource)
                {
                    Animation::AnimationGraphDocument graph(*binding->graphSource);
                    for (const auto& field : graph.GetFields()) dump["graphFields"].push_back({{"section", field.section}, {"index", field.index}, {"name", field.name}, {"value", field.value}});
                }
                std::vector<vec3> values;
                if (deformation->GetControllerParameterValues(binding->controller, values))
                    for (const auto& value : values) dump["parameters"].push_back({value.x, value.y, value.z});
            }
            const fs::path directory = fs::absolute("../Source/Resources/Automation/Artifacts/animation-diagnostics");
            fs::create_directories(directory);
            const auto path = directory / (name + ".json");
            std::ofstream stream(path, std::ios::binary | std::ios::trunc);
            stream << dump.dump(2); stream.close();
            success = static_cast<bool>(stream);
            error = success ? path.generic_string() : "Diagnostic dump could not be written";
        }
        catch (const std::exception& exception) { error = exception.what(); }
        zenith->Push(success); zenith->Push(error.c_str()); return 2;
    }

    i32 AssetHandler::AnimationAssetDocument(Zenith* zenith)
    {
        std::shared_ptr<AnimationPreviewBinding> binding;
        auto* deformation = GetControllerBinding(zenith->CheckVal<u32>(1), binding, false);
        if (!deformation) { zenith->Push(); return 1; }
        if (zenith->IsString(2))
        {
            std::string path;
            FileFormat::AssetID id = FileFormat::INVALID_ASSET_ID;
            auto* storage = ServiceLocator::GetPactStorage();
            const u32 type = zenith->CheckVal<u32>(3);
            std::string error;
            PACT::PactFileHandle file;
            auto document = std::make_unique<Animation::AnimationAssetDocument>();
            const bool opened = type <= static_cast<u32>(Animation::AnimationDocumentType::IK) && storage && ResolveModelAssetPath(zenith->CheckVal<const char*>(2), storage, path, id) && storage->ReadFile(id, file) == PACT::PactReadResult::Success && document->Open(static_cast<Animation::AnimationDocumentType>(type), {reinterpret_cast<const u8*>(file.GetData()), file.GetSize()}, error);
            if (!opened) { zenith->Push(); zenith->Push(error.empty() ? "Asset could not be opened" : error.c_str()); return 2; }
            binding->assetDocument = std::move(document);
            binding->assetDocumentPath = path;
            binding->authoredAssetID = id;
            binding->assetDocumentStatus = "Opened; edits remain in the draft until Apply";
        }
        if (!binding->assetDocument) { zenith->Push(); return 1; }
        const auto& document = *binding->assetDocument;
        zenith->CreateTable();
        zenith->AddTableField("path", binding->assetDocumentPath.c_str());
        zenith->AddTableField("type", static_cast<u32>(document.GetType()));
        zenith->AddTableField("revision", document.GetRevision());
        zenith->AddTableField("dirty", document.IsDirty());
        zenith->AddTableField("status", binding->assetDocumentStatus.c_str());
        zenith->AddTableField("publication", deformation->GetAnimationAssetPublicationStatus().c_str());
        zenith->CreateTable();
        u32 index = 1;
        for (const auto& field : document.GetFields())
        {
            zenith->CreateTable();
            zenith->AddTableField("path", field.path.c_str());
            zenith->AddTableField("value", field.value.c_str());
            zenith->SetTableKey(index++);
        }
        zenith->SetTableKey("fields");
        auto& assets = ServiceLocator::GetGameRenderer()->GetRenderAssetResources()->GetAnimationRegistry();
        const auto hierarchy = AuthoringHierarchy(document, deformation->GetPoseSource(binding->poseSource), assets);
        const auto& names = AuthoringJointNames();
        AnimationLoading::RigFamilyHandle namingFamily;
        if (const auto* pose = deformation->GetPoseSource(binding->poseSource))
            if (const auto* skeleton = assets.GetRetained(pose->skeleton); skeleton && skeleton->root.rigFamilyAssetID != FileFormat::INVALID_ASSET_ID)
                namingFamily = assets.LoadRigFamily(skeleton->root.rigFamilyAssetID);
        const auto* familyNames = namingFamily ? assets.Get(namingFamily) : nullptr;
        zenith->CreateTable();
        for (u32 i = 0; i < hierarchy.size(); ++i)
        {
            const auto& joint = hierarchy[i];
            const auto found = names.find(joint.semanticID);
            auto label = found != names.end() ? found->second : "Joint " + std::to_string(i);
            if (found == names.end() && familyNames && joint.familyJointIndex >= 0 && static_cast<u32>(joint.familyJointIndex) < familyNames->joints.size())
                if (const auto semantic = names.find(familyNames->joints[joint.familyJointIndex].semanticID); semantic != names.end()) label += " / " + semantic->second;
            zenith->CreateTable();
            zenith->AddTableField("index", i);
            zenith->AddTableField("parent", joint.parentJointIndex);
            zenith->AddTableField("name", label.c_str());
            zenith->AddTableField("semanticID", joint.semanticID);
            zenith->AddTableField("familyIndex", joint.familyJointIndex);
            zenith->SetTableKey(i + 1);
        }
        zenith->SetTableKey("hierarchy");
        if (namingFamily) assets.Release(namingFamily);
        return 1;
    }

    i32 AssetHandler::AnimationAssetEditField(Zenith* zenith)
    {
        std::shared_ptr<AnimationPreviewBinding> binding;
        auto* deformation = GetControllerBinding(zenith->CheckVal<u32>(1), binding, false);
        std::string error = "No asset document is open";
        const bool success = deformation && binding->assetDocument && binding->assetDocument->Edit(zenith->CheckVal<const char*>(2), zenith->CheckVal<const char*>(3), error);
        if (binding) binding->assetDocumentStatus = success ? "Draft changed" : error;
        zenith->Push(success); zenith->Push(error.c_str()); return 2;
    }

    i32 AssetHandler::AnimationAssetCommand(Zenith* zenith)
    {
        std::shared_ptr<AnimationPreviewBinding> binding;
        auto* deformation = GetControllerBinding(zenith->CheckVal<u32>(1), binding, false);
        auto* document = binding ? binding->assetDocument.get() : nullptr;
        const std::string command = zenith->CheckVal<const char*>(2);
        bool success = false;
        std::string error;
        if (!deformation || !document) error = "No asset document is open";
        else if (command == "undo") success = document->Undo();
        else if (command == "redo") success = document->Redo();
        else if (command == "revert") { document->Revert(); success = true; }
        else if (command == "insert") success = document->Insert(zenith->CheckVal<const char*>(3), error);
        else if (command == "remove") success = document->Remove(zenith->CheckVal<const char*>(3), zenith->CheckVal<u32>(4), error);
        else if (command == "compile" || command == "apply")
        {
            success = document->Compile(error);
            if (success && command == "apply") success = deformation->QueueAnimationAsset(document->GetType(), binding->authoredAssetID, document->GetPayload(), error);
        }
        else if (command == "paint" || command == "mirror")
        {
            const auto* pose = deformation->GetPoseSources().Get(binding->poseSource);
            auto& assets = ServiceLocator::GetGameRenderer()->GetRenderAssetResources()->GetAnimationRegistry();
            const auto joints = AuthoringHierarchy(*document, pose, assets);
            if (command == "paint") success = document->PaintMask(joints, zenith->CheckVal<u32>(3), zenith->CheckVal<f32>(4), zenith->CheckVal<f32>(5), zenith->CheckVal<bool>(6), error);
            else
            {
                const auto& names = AuthoringJointNames();
                std::vector<std::pair<u32, u32>> pairs;
                const std::string sourceSide = zenith->CheckVal<const char*>(3);
                const std::string targetSide = sourceSide == "left" ? "right" : "left";
                for (u32 i = 0; i < joints.size(); ++i)
                {
                    const auto source = names.find(joints[i].semanticID);
                    if (source == names.end() || !source->second.starts_with(sourceSide)) continue;
                    const auto targetName = targetSide + source->second.substr(sourceSide.size());
                    for (u32 j = 0; j < joints.size(); ++j)
                    {
                        const auto target = names.find(joints[j].semanticID);
                        if (target != names.end() && target->second == targetName) pairs.emplace_back(i, j);
                    }
                }
                if (pairs.empty()) error = "No reviewed semantic mirror pairs exist for this hierarchy";
                else success = document->MirrorMask(pairs, error);
            }
        }
        else if (command == "save" || command == "load")
        {
            const std::string name = zenith->CheckVal<const char*>(3);
            if (name.empty() || name.size() > 64 || !std::all_of(name.begin(), name.end(), [](unsigned char c) { return std::isalnum(c) || c == '-' || c == '_'; })) error = "Use letters, numbers, hyphens or underscores for the source name";
            else try
            {
                const fs::path directory = GetAnimationSourceDirectory();
                const fs::path path = directory / (name + ".animationassetsource");
                if (command == "save")
                {
                    fs::create_directories(directory);
                    const fs::path temporary = directory / (name + ".animationassetsource.tmp");
                    std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
                    const auto bytes = document->Serialize();
                    stream.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
                    stream.close();
                    if (stream) { fs::rename(temporary, path); document->MarkSaved(); success = true; }
                    else error = "Write failed; previous source retained";
                }
                else if (fs::file_size(path) <= 4 * 1024 * 1024)
                {
                    std::ifstream stream(path, std::ios::binary);
                    const std::vector<u8> bytes((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
                    success = stream.good() && document->LoadSource(bytes, error);
                    if (success) document->MarkSaved();
                }
                else error = "Source exceeds the editor size limit";
            }
            catch (const std::exception& exception) { error = exception.what(); }
        }
        else error = "Unknown asset document command";
        if (binding) binding->assetDocumentStatus = success ? command + " succeeded" : error;
        zenith->Push(success); zenith->Push(error.c_str()); return 2;
    }

    i32 AssetHandler::AnimationGraphDocument(Zenith* zenith)
    {
        std::shared_ptr<AnimationPreviewBinding> binding;
        auto* deformation = GetControllerBinding(zenith->CheckVal<u32>(1), binding);
        auto* document = GetGraphDocument(deformation, binding);
        if (!document)
        {
            zenith->Push();
            return 1;
        }
        RefreshGraphPublication(*deformation, *binding);
        zenith->CreateTable();
        zenith->AddTableField("revision", document->GetRevision());
        zenith->AddTableField("dirty", document->IsDirty());
        zenith->AddTableField("canUndo", document->CanUndo());
        zenith->AddTableField("canRedo", document->CanRedo());
        zenith->AddTableField("pending", binding->pendingCompiled != nullptr);
        zenith->AddTableField("status", binding->authoringStatus.c_str());
        zenith->AddTableField("liveHash", binding->compiledGraph->contentHash);
        zenith->CreateTable();
        u32 fieldIndex = 1;
        for (const auto& field : document->GetFields())
        {
            zenith->CreateTable();
            zenith->AddTableField("section", field.section.c_str());
            zenith->AddTableField("index", field.index);
            zenith->AddTableField("name", field.name.c_str());
            zenith->AddTableField("value", field.value.c_str());
            zenith->SetTableKey(fieldIndex++);
        }
        zenith->SetTableKey("fields");
        zenith->CreateTable();
        u32 index = 1;
        for (const auto& diagnostic : document->GetCompilation().diagnostics)
        {
            zenith->CreateTable();
            zenith->AddTableField("field", diagnostic.field.c_str());
            zenith->AddTableField("index", diagnostic.index);
            zenith->AddTableField("message", diagnostic.message.c_str());
            zenith->SetTableKey(index++);
        }
        zenith->SetTableKey("diagnostics");
        return 1;
    }

    i32 AssetHandler::AnimationGraphEditField(Zenith* zenith)
    {
        std::shared_ptr<AnimationPreviewBinding> binding;
        auto* deformation = GetControllerBinding(zenith->CheckVal<u32>(1), binding);
        auto* document = GetGraphDocument(deformation, binding);
        std::string error;
        const bool success = document && document->EditField(zenith->CheckVal<const char*>(2), zenith->CheckVal<u32>(3), zenith->CheckVal<const char*>(4), zenith->CheckVal<const char*>(5), error);
        if (binding)
            binding->authoringStatus = success ? "Draft changed; Apply to publish" : error;
        zenith->Push(success);
        zenith->Push(error.c_str());
        return 2;
    }

    i32 AssetHandler::AnimationGraphCommand(Zenith* zenith)
    {
        std::shared_ptr<AnimationPreviewBinding> binding;
        auto* deformation = GetControllerBinding(zenith->CheckVal<u32>(1), binding);
        auto* document = GetGraphDocument(deformation, binding);
        const std::string command = zenith->CheckVal<const char*>(2);
        std::string error;
        bool success = false;
        if (!document)
            error = "No graph is bound";
        else if (command == "undo") success = document->Undo();
        else if (command == "redo") success = document->Redo();
        else if (command == "revert") { document->Revert(); success = true; }
        else if (command == "duplicate") success = document->DuplicateItem(zenith->CheckVal<const char*>(3), zenith->CheckVal<u32>(4), error);
        else if (command == "add") success = document->DuplicateItem(zenith->CheckVal<const char*>(3), std::numeric_limits<u32>::max(), error);
        else if (command == "connect")
        {
            auto source = document->GetSource();
            const u32 from = zenith->CheckVal<u32>(3);
            const u32 to = zenith->CheckVal<u32>(4);
            const auto parameter = std::find_if(source.parameters.begin(), source.parameters.end(), [](const auto& value) { return value.type == FileFormat::Animation::GraphValueType::Float; });
            if (from >= source.states.size() || to >= source.states.size() || from == to || parameter == source.parameters.end()) error = "Choose two different states and author a float condition parameter";
            else
            {
                Animation::EditableGraphTransition transition;
                transition.sourceGUID = source.states[from].sourceGUID;
                transition.targetGUID = source.states[to].sourceGUID;
                transition.parameter = parameter->name;
                transition.durationMicroseconds = 200000;
                source.transitions.push_back(transition);
                document->Edit(std::move(source)); success = true;
            }
        }
        else if (command == "remove") success = document->RemoveItem(zenith->CheckVal<const char*>(3), zenith->CheckVal<u32>(4), error);
        else if (command == "apply" || command == "compile")
        {
            success = document->Compile();
            if (!success)
                error = "Compilation failed; running graph unchanged";
            else if (command == "apply")
            {
                success = deformation->QueueControllerGraph(binding->controller, document->GetLastGoodGraph(), error);
                if (success)
                {
                    binding->pendingSource = std::make_shared<const Animation::EditableAnimationGraph>(document->GetSource());
                    binding->pendingCompiled = document->GetLastGoodGraph();
                }
            }
        }
        else if (command == "save" || command == "load")
        {
            const std::string name = zenith->CheckVal<const char*>(3);
            const bool validName = !name.empty() && name.size() <= 64 && std::all_of(name.begin(), name.end(), [](unsigned char c) { return std::isalnum(c) || c == '-' || c == '_'; });
            if (!validName)
                error = "Use a document name containing only letters, numbers, hyphens or underscores";
            else
            {
                try
                {
                    const fs::path directory = GetAnimationSourceDirectory();
                    const fs::path path = directory / (name + ".animationgraphsource");
                    if (command == "save")
                    {
                        fs::create_directories(directory);
                        const fs::path temporary = directory / (name + ".animationgraphsource.tmp");
                        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
                        const auto bytes = Animation::AnimationGraphDocument::Serialize(document->GetSource());
                        stream.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
                        stream.close();
                        if (!stream)
                            error = "Could not write graph source; previous save retained";
                        else
                        {
                            fs::rename(temporary, path);
                            document->MarkSaved();
                            success = true;
                        }
                    }
                    else if (fs::file_size(path) <= 4 * 1024 * 1024)
                    {
                        std::ifstream stream(path, std::ios::binary);
                        const std::vector<u8> bytes((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
                        Animation::EditableAnimationGraph source;
                        success = stream.good() && Animation::AnimationGraphDocument::Parse(bytes, source, error);
                        if (success)
                        {
                            document->Edit(std::move(source));
                            document->MarkSaved();
                        }
                    }
                    else error = "Graph source exceeds the 4 MiB editor limit";
                }
                catch (const std::exception& exception) { error = exception.what(); }
            }
        }
        else error = "Unknown graph document command";
        if (binding)
            binding->authoringStatus = success ? (command == "apply" ? "Publication queued" : command + " succeeded") : error;
        zenith->Push(success);
        zenith->Push(error.c_str());
        return 2;
    }

    i32 AssetHandler::GetAnimationControllerGraph(Zenith* zenith)
    {
        std::shared_ptr<AnimationPreviewBinding> binding;
        Animation::ModelDeformationSystem* deformation = GetControllerBinding(zenith->CheckVal<u32>(1), binding);
        if (!deformation || !binding->graphSource || !binding->compiledGraph)
        {
            zenith->Push();
            return 1;
        }

        RefreshGraphPublication(*deformation, *binding);
        const bool draft = zenith->IsBoolean(2) && zenith->ToBoolean(2);
        auto* document = draft ? GetGraphDocument(deformation, binding) : nullptr;
        if (draft && !document) { zenith->Push(); return 1; }
        const bool validDraft = !draft || document->Compile();
        const Animation::EditableAnimationGraph& source = draft ? document->GetSource() : *binding->graphSource;
        const Animation::CompiledAnimationGraph& compiled = draft && validDraft ? *document->GetLastGoodGraph() : *binding->compiledGraph;
        auto stateIndexForGUID = [&source](u64 guid) { const auto found = std::find_if(source.states.begin(), source.states.end(), [guid](const auto& state) { return state.sourceGUID == guid; }); return found == source.states.end() ? std::numeric_limits<u32>::max() : static_cast<u32>(found - source.states.begin()); };
        zenith->CreateTable();
        zenith->AddTableField("name", source.displayName.c_str());
        zenith->AddTableField("version", source.version);
        zenith->AddTableField("contentHash", compiled.contentHash);
        zenith->AddTableField("revision", binding->publicationRevision);
        zenith->AddTableField("entryState", stateIndexForGUID(source.entryStateGUID));
        zenith->AddTableField("validDraft", validDraft);

        zenith->CreateTable();
        for (u32 parameterIndex = 0; parameterIndex < source.parameters.size(); ++parameterIndex)
        {
            const Animation::EditableGraphParameter& parameter = source.parameters[parameterIndex];
            zenith->CreateTable();
            zenith->AddTableField("index", parameterIndex);
            zenith->AddTableField("name", parameter.name.c_str());
            zenith->AddTableField("type", static_cast<u32>(parameter.type));
            zenith->AddTableField("defaultFloat", parameter.defaultFloat);
            zenith->AddTableField("defaultInt", parameter.defaultInt);
            zenith->AddTableField("defaultBool", parameter.defaultBool);
            zenith->AddTableField("defaultX", parameter.defaultFloat3.x);
            zenith->AddTableField("defaultY", parameter.defaultFloat3.y);
            zenith->AddTableField("defaultZ", parameter.defaultFloat3.z);
            zenith->AddTableField("oneFrameTrigger", parameter.oneFrameTrigger);
            zenith->SetTableKey(parameterIndex + 1);
        }
        zenith->SetTableKey("parameters");

        zenith->CreateTable();
        for (u32 stateIndex = 0; stateIndex < source.states.size(); ++stateIndex)
        {
            const Animation::EditableGraphState& state = source.states[stateIndex];
            zenith->CreateTable();
            zenith->AddTableField("index", stateIndex);
            zenith->AddTableField("sourceGUID", state.sourceGUID);
            zenith->AddTableField("name", state.displayName.c_str());
            zenith->AddTableField("kind", static_cast<u32>(state.kind));
            zenith->AddTableField("clipSlotSemanticID", state.clipSlotSemanticID);
            zenith->AddTableField("durationUS", state.durationMicroseconds);
            zenith->AddTableField("inputX", state.inputX.c_str());
            zenith->AddTableField("inputY", state.inputY.c_str());
            zenith->AddTableField("smoothingHalfLife", state.smoothingHalfLifeSeconds);
            zenith->AddTableField("wrapX", state.wrapX);
            zenith->AddTableField("wrapMinimum", state.wrapMinimum);
            zenith->AddTableField("wrapMaximum", state.wrapMaximum);
            zenith->AddTableField("positionX", state.editorPosition.x);
            zenith->AddTableField("positionY", state.editorPosition.y);

            zenith->CreateTable();
            for (u32 sampleIndex = 0; sampleIndex < state.blendSamples.size(); ++sampleIndex)
            {
                const Animation::EditableGraphBlendSample& sample = state.blendSamples[sampleIndex];
                zenith->CreateTable();
                zenith->AddTableField("index", sampleIndex);
                zenith->AddTableField("name", sample.displayName.c_str());
                zenith->AddTableField("x", sample.position.x);
                zenith->AddTableField("y", sample.position.y);
                zenith->AddTableField("clipSlotSemanticID", sample.clipSlotSemanticID);
                zenith->SetTableKey(sampleIndex + 1);
            }
            zenith->SetTableKey("samples");

            zenith->CreateTable();
            for (u32 triangleIndex = 0; triangleIndex < state.blendTriangles.size(); ++triangleIndex)
            {
                const Animation::BlendSpaceTriangle& triangle = state.blendTriangles[triangleIndex];
                zenith->CreateTable();
                zenith->AddTableField("a", triangle.samples[0]);
                zenith->AddTableField("b", triangle.samples[1]);
                zenith->AddTableField("c", triangle.samples[2]);
                zenith->SetTableKey(triangleIndex + 1);
            }
            zenith->SetTableKey("triangles");
            zenith->SetTableKey(stateIndex + 1);
        }
        zenith->SetTableKey("states");

        zenith->CreateTable();
        for (u32 transitionIndex = 0; transitionIndex < source.transitions.size(); ++transitionIndex)
        {
            const Animation::EditableGraphTransition& transition = source.transitions[transitionIndex];
            zenith->CreateTable();
            zenith->AddTableField("index", transitionIndex);
            zenith->AddTableField("sourceState", stateIndexForGUID(transition.sourceGUID));
            zenith->AddTableField("targetState", stateIndexForGUID(transition.targetGUID));
            zenith->AddTableField("sourceGUID", transition.sourceGUID);
            zenith->AddTableField("targetGUID", transition.targetGUID);
            zenith->AddTableField("parameter", transition.parameter.c_str());
            zenith->AddTableField("comparison", static_cast<u32>(transition.comparison));
            zenith->AddTableField("threshold", transition.threshold);
            zenith->AddTableField("durationUS", transition.durationMicroseconds);
            zenith->AddTableField("curve", static_cast<u32>(transition.curve));
            zenith->SetTableKey(transitionIndex + 1);
        }
        zenith->SetTableKey("transitions");

        zenith->CreateTable();
        for (u32 operationIndex = 0; operationIndex < source.ikOperations.size(); ++operationIndex)
        {
            const Animation::EditableGraphIKOperation& operation = source.ikOperations[operationIndex];
            zenith->CreateTable();
            zenith->AddTableField("index", operationIndex);
            zenith->AddTableField("name", operation.displayName.c_str());
            zenith->AddTableField("rigAssetID", operation.rigAssetID);
            zenith->AddTableField("solverSemanticID", operation.solverSemanticID);
            zenith->AddTableField("goalParameter", operation.goalParameter.c_str());
            zenith->AddTableField("goalAttachmentSemanticID", operation.goalAttachmentSemanticID);
            zenith->AddTableField("effectorAttachmentSemanticID", operation.effectorAttachmentSemanticID);
            zenith->AddTableField("weightParameter", operation.weightParameter.c_str());
            zenith->AddTableField("constantWeight", operation.constantWeight);
            zenith->AddTableField("executionOrder", operation.executionOrder);
            zenith->SetTableKey(operationIndex + 1);
        }
        zenith->SetTableKey("ikOperations");
        return 1;
    }

    i32 AssetHandler::DestroyAnimatedModel(Zenith* zenith)
    {
        const u32 entityValue = zenith->CheckVal<u32>(1);
        const bool hadAnimationBinding = s_animationBindingByEntity.contains(entityValue);
        ReleaseModelAttachment(entityValue);
        ReleaseChildAttachments(entityValue);
        ReleaseAnimationBinding(entityValue, true);
        bool destroyed = hadAnimationBinding;
        EnttRegistries* registries = ServiceLocator::GetEnttRegistries();
        const entt::entity entity = static_cast<entt::entity>(entityValue);
        if (registries && registries->gameRegistry->valid(entity))
        {
            if (ECS::Components::Model* model = registries->gameRegistry->try_get<ECS::Components::Model>(entity))
                ServiceLocator::GetGameRenderer()->GetModelLoader()->UnloadModelForEntity(entity, *model);
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
        const Animation::ControllerPosePlayerStats& controllerStats = deformation->GetControllerPlaybackStats();
        const Animation::AnimationControllerStats controllerCoreStats = deformation->GetControllerCoreStats();
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
        zenith->AddTableField("animationViewRequests", deformationStats.viewRequests);
        zenith->AddTableField("animationTransientViewRequests", deformationStats.transientViewRequests);
        zenith->AddTableField("animationShadowViewRequests", deformationStats.shadowViewRequests);
        zenith->AddTableField("animationFrustumRejectedInstances", deformationStats.frustumRejectedInstances);
        zenith->AddTableField("animationSelectedSegmentBounds", deformationStats.selectedSegmentBounds);
        zenith->AddTableField("animationConservativeBoundsFallbacks", deformationStats.conservativeBoundsFallbacks);
        zenith->AddTableField("animationDebugActualBounds", deformationStats.debugActualBounds);
        zenith->AddTableField("animationBoundsViolations", deformationStats.boundsViolations);
        zenith->AddTableField("animationDemandedLODs", deformationStats.demandedLODs);
        zenith->AddTableField("animationMotionContinuous", deformationStats.motionContinuous);
        zenith->AddTableField("animationMotionHeld", deformationStats.motionHeld);
        zenith->AddTableField("animationMotionInvalid", deformationStats.motionInvalid);
        zenith->AddTableField("animationMotionScheduledUpdates", deformationStats.motionScheduledUpdates);
        zenith->AddTableField("animationMotionReactivated", deformationStats.motionReactivated);
        zenith->AddTableField("animationMotionTeleported", deformationStats.motionTeleported);
        zenith->AddTableField("animationMotionGenerationInvalid", deformationStats.motionGenerationInvalid);
        zenith->AddTableField("animationControllerAdvances", playbackStats.controllerAdvances);
        zenith->AddTableField("animationControllerPoseRequests", playbackStats.poseRequests);
        zenith->AddTableField("animationControllerPoseEvaluations", playbackStats.poseEvaluations);
        zenith->AddTableField("animationControllerEvaluationUS", playbackStats.evaluationMicroseconds);
        zenith->AddTableField("animationGraphAdvances", controllerStats.advances);
        zenith->AddTableField("animationGraphPoseRequests", controllerStats.poseRequests);
        zenith->AddTableField("animationGraphPoseEvaluations", controllerStats.poseEvaluations);
        zenith->AddTableField("animationGraphBlendedSamples", controllerStats.blendedSamples);
        zenith->AddTableField("animationGraphSnapshotBlends", controllerStats.snapshotBlends);
        zenith->AddTableField("animationLayerOperations", controllerStats.layerOperations);
        zenith->AddTableField("animationIKOperations", controllerStats.ikOperations);
        zenith->AddTableField("animationIKClampedGoals", controllerStats.ikClampedGoals);
        zenith->AddTableField("animationGraphFailures", controllerStats.failures);
        zenith->AddTableField("animationFamilyConversions", controllerStats.familyConversions);
        zenith->AddTableField("animationCachedPoseHits", controllerStats.cachedPoseHits);
        zenith->AddTableField("animationActiveDomains", controllerStats.lastActiveDomains);
        zenith->AddTableField("animationPeakActiveDomains", controllerStats.peakActiveDomains);
        const auto& referenceStats = ServiceLocator::GetGameRenderer()->GetSkeletonDebugDraw()->GetStats();
        zenith->AddTableField("animationReferenceCompared", referenceStats.referenceCompared);
        zenith->AddTableField("animationReferencePositionError", referenceStats.maximumReferencePositionError);
        zenith->AddTableField("animationReferenceAngleError", referenceStats.maximumReferenceAngleError);
        zenith->AddTableField("animationGraphTransitions", controllerCoreStats.transitions);
        zenith->AddTableField("animationGraphInterruptions", controllerCoreStats.interruptions);
        zenith->AddTableField("animationGraphSnapshots", controllerCoreStats.snapshots);
        zenith->AddTableField("animationCommandOverflows", controllerCoreStats.queueOverflows);
        zenith->AddTableField("animationAuthoritativeEventOverflows", controllerCoreStats.authoritativeEventOverflows);
        zenith->AddTableField("animationCosmeticEventDrops", controllerCoreStats.cosmeticEventDrops);
        zenith->AddTableField("animationRejectedWrites", controllerCoreStats.rejectedWrites);
        zenith->AddTableField("animationStaleHandles", controllerCoreStats.staleHandleRejects);
        const auto assetStats = ServiceLocator::GetGameRenderer()->GetRenderAssetResources()->GetAnimationRegistry().GetStats();
        zenith->AddTableField("animationAssetResidentBytes", assetStats.residentBytes);
        zenith->AddTableField("animationAssetReferences", assetStats.references);
        zenith->AddTableField("animationAssetReloads", assetStats.reloads);
        zenith->AddTableField("animationAssetFailures", assetStats.failures);
        zenith->AddTableField("animationAssetCacheHits", assetStats.cacheHits);
        zenith->CreateTable();
        u32 failureIndex = 1;
        for (const auto& failure : ServiceLocator::GetGameRenderer()->GetRenderAssetResources()->GetAnimationRegistry().GetFailureDescriptions())
        {
            zenith->Push(failure.c_str());
            zenith->SetTableKey(failureIndex++);
        }
        zenith->SetTableKey("animationAssetFailureDetails");
        for (u32 lod = 0; lod < 8; ++lod)
        {
            const std::string field = "modelLOD" + std::to_string(lod);
            zenith->AddTableField(field.c_str(), modelStats.lodSelections[lod]);
        }
        return 1;
    }
}
