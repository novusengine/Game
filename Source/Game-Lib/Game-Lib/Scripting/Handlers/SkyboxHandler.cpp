#include "SkyboxHandler.h"
#include "Game-Lib/Application/EnttRegistries.h"
#include "Game-Lib/ECS/Components/Name.h"
#include "Game-Lib/ECS/Components/Model.h"
#include "Game-Lib/ECS/Singletons/Skybox.h"
#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Rendering/Model/ModelLoader.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Scripting/LuaManager.h>
#include <Scripting/Zenith.h>

#include <entt/entt.hpp>
#include <lualib.h>

#include <filesystem>

namespace fs = std::filesystem;

namespace Scripting::Skybox
{
    static const fs::path complexModelPath = fs::path("Data/ComplexModel/");
    static const fs::path skyboxFolderPath = complexModelPath / "environments/stars";
    static const fs::path skyboxExtension = ".complexmodel";

    void SkyboxHandler::Register(Zenith* zenith)
    {
        LuaManager* luaManager = ServiceLocator::GetLuaManager();
        const bool inDeveloperMode = luaManager && luaManager->IsDeveloperMode();
        const Scripting::LuaMethodFlags excludeFlags = inDeveloperMode
            ? Scripting::LuaMethodFlags::None
            : Scripting::LuaMethodFlags::DeveloperOnly;

        LuaMethodTable::Set(zenith, skyboxGlobalMethods, "Skybox", excludeFlags);
    }

    static ECS::Singletons::Skybox* GetSkybox()
    {
        EnttRegistries* registries = ServiceLocator::GetEnttRegistries();
        if (!registries || !registries->gameRegistry)
            return nullptr;
        return &registries->gameRegistry->ctx().get<ECS::Singletons::Skybox>();
    }

    // Returns the currently loaded skybox's full model name, or nil if nothing is loaded.
    i32 SkyboxHandler::GetCurrent(Zenith* zenith)
    {
        EnttRegistries* registries = ServiceLocator::GetEnttRegistries();
        ECS::Singletons::Skybox* skybox = GetSkybox();
        if (!skybox || !registries->gameRegistry->valid(skybox->entity))
        {
            zenith->Push();
            return 1;
        }

        ECS::Components::Name* name = registries->gameRegistry->try_get<ECS::Components::Name>(skybox->entity);
        if (name && !name->fullName.empty())
            zenith->Push(name->fullName.c_str());
        else
            zenith->Push();
        return 1;
    }

    // Returns an array of { name, path } for every .complexmodel under environments/stars. `path` is
    // relative to Data/ComplexModel/ so it can be passed straight back to Load().
    i32 SkyboxHandler::Enumerate(Zenith* zenith)
    {
        zenith->CreateTable();
        i32 index = 0;

        std::error_code ec;
        if (fs::is_directory(skyboxFolderPath, ec))
        {
            for (const auto& entry : fs::recursive_directory_iterator(skyboxFolderPath, ec))
            {
                if (!entry.is_regular_file() || entry.path().extension() != skyboxExtension)
                    continue;

                fs::path rel = fs::relative(entry.path(), complexModelPath);
                zenith->CreateTable();
                zenith->AddTableField("name", entry.path().stem().string().c_str());
                zenith->AddTableField("path", rel.generic_string().c_str());
                zenith->SetTableKey(++index);
            }
        }
        return 1;
    }

    i32 SkyboxHandler::Load(Zenith* zenith)
    {
        if (!zenith->IsString(1))
            return 0;
        const char* relPath = zenith->Get<const char*>(1);

        ECS::Singletons::Skybox* skybox = GetSkybox();
        EnttRegistries* registries = ServiceLocator::GetEnttRegistries();
        if (!skybox || !relPath || !registries->gameRegistry->valid(skybox->entity))
            return 0;

        ModelLoader* modelLoader = ServiceLocator::GetGameRenderer()->GetModelLoader();
        auto& model = registries->gameRegistry->get<ECS::Components::Model>(skybox->entity);
        u32 modelHash = modelLoader->GetModelHashFromModelPath(relPath);
        modelLoader->LoadModelForEntity(skybox->entity, model, modelHash);
        return 0;
    }

    i32 SkyboxHandler::Unload(Zenith* /*zenith*/)
    {
        ECS::Singletons::Skybox* skybox = GetSkybox();
        EnttRegistries* registries = ServiceLocator::GetEnttRegistries();
        if (!skybox || !registries->gameRegistry->valid(skybox->entity))
            return 0;

        ModelLoader* modelLoader = ServiceLocator::GetGameRenderer()->GetModelLoader();
        auto& model = registries->gameRegistry->get<ECS::Components::Model>(skybox->entity);
        modelLoader->UnloadModelForEntity(skybox->entity, model);
        return 0;
    }
}
