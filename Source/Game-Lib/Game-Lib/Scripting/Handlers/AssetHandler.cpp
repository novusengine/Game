#include "AssetHandler.h"
#include "Game-Lib/Application/EnttRegistries.h"
#include "Game-Lib/ECS/Components/AABB.h"
#include "Game-Lib/ECS/Components/Model.h"
#include "Game-Lib/ECS/Components/Name.h"
#include "Game-Lib/ECS/Singletons/ActiveCamera.h"
#include "Game-Lib/ECS/Singletons/EditorSelection.h"
#include "Game-Lib/ECS/Util/Transforms.h"
#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Rendering/Model/ModelLoader.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Filesystem/PactStorage.h>
#include <Scripting/LuaManager.h>
#include <Scripting/Zenith.h>

#include <entt/entt.hpp>
#include <lualib.h>

#include <algorithm>
#include <filesystem>
#include <map>
#include <set>
#include <string>

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
}
