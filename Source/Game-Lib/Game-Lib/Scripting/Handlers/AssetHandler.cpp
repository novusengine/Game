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

#include <Scripting/LuaManager.h>
#include <Scripting/Zenith.h>

#include <entt/entt.hpp>
#include <lualib.h>

#include <algorithm>
#include <filesystem>
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

        u32 modelPathHash = ServiceLocator::GetGameRenderer()->GetModelLoader()->GetModelHashFromModelPath(modelPath);
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
