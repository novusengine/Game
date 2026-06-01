#include "CameraHandler.h"
#include "Game-Lib/Application/EnttRegistries.h"
#include "Game-Lib/ECS/Components/Camera.h"
#include "Game-Lib/ECS/Singletons/ActiveCamera.h"
#include "Game-Lib/ECS/Singletons/Database/ClientDBSingleton.h"
#include "Game-Lib/ECS/Singletons/FreeflyingCameraSettings.h"
#include "Game-Lib/ECS/Util/Database/CameraUtil.h"
#include "Game-Lib/ECS/Util/Transforms.h"
#include "Game-Lib/Scripting/Util/ZenithUtil.h"
#include "Game-Lib/Util/MapUtil.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Base/CVarSystem/CVarSystem.h>
#include <Base/Util/StringUtils.h>

#include <FileFormat/Shared.h>

#include <MetaGen/Game/Lua/Lua.h>
#include <MetaGen/Shared/ClientDB/ClientDB.h>

#include <Scripting/LuaManager.h>
#include <Scripting/Zenith.h>

#include <entt/entt.hpp>
#include <lualib.h>

#include <algorithm>
#include <string>
#include <vector>

namespace Scripting::Camera
{
    // Persists across runs (config file), so a flagged camera save is auto-loaded on the next launch.
    AutoCVar_String CVAR_CameraStartupSave(CVarCategory::Client, "cameraStartupSave", "Camera save name to auto-goto on startup", "");

    void CameraHandler::Register(Zenith* zenith)
    {
        LuaManager* luaManager = ServiceLocator::GetLuaManager();
        const bool inDeveloperMode = luaManager && luaManager->IsDeveloperMode();
        const Scripting::LuaMethodFlags excludeFlags = inDeveloperMode
            ? Scripting::LuaMethodFlags::None
            : Scripting::LuaMethodFlags::DeveloperOnly;

        LuaMethodTable::Set(zenith, cameraGlobalMethods, "Camera", excludeFlags);

        _onTransformChangedRef = LUA_NOREF;
        _onSavesChangedRef = LUA_NOREF;
    }

    void CameraHandler::Clear(Zenith* zenith)
    {
        _onTransformChangedRef = LUA_NOREF;
        _onSavesChangedRef = LUA_NOREF;
    }

    static CameraHandler* GetSelf()
    {
        LuaManager* luaManager = ServiceLocator::GetLuaManager();
        if (!luaManager)
            return nullptr;
        return luaManager->GetLuaHandler<CameraHandler>(static_cast<LuaHandlerID>(MetaGen::Game::Lua::LuaHandlerTypeEnum::Camera));
    }

    struct ActiveCameraView
    {
        entt::registry* registry = nullptr;
        ECS::Components::Transform* transform = nullptr;
        ECS::Components::Camera* camera = nullptr;
    };

    static ActiveCameraView GetActiveCamera()
    {
        ActiveCameraView view{};

        EnttRegistries* registries = ServiceLocator::GetEnttRegistries();
        if (!registries || !registries->gameRegistry)
            return view;

        entt::registry& gameRegistry = *registries->gameRegistry;
        entt::registry::context& ctx = gameRegistry.ctx();

        if (!ctx.contains<ECS::Singletons::ActiveCamera>())
            return view;

        auto& activeCamera = ctx.get<ECS::Singletons::ActiveCamera>();
        if (activeCamera.entity == entt::null)
            return view;

        view.registry = &gameRegistry;
        view.transform = gameRegistry.try_get<ECS::Components::Transform>(activeCamera.entity);
        view.camera = gameRegistry.try_get<ECS::Components::Camera>(activeCamera.entity);
        return view;
    }

    static ECS::Singletons::FreeflyingCameraSettings* GetFreeflySettings()
    {
        EnttRegistries* registries = ServiceLocator::GetEnttRegistries();
        if (!registries || !registries->gameRegistry)
            return nullptr;
        auto& ctx = registries->gameRegistry->ctx();
        if (!ctx.contains<ECS::Singletons::FreeflyingCameraSettings>())
            return nullptr;
        return &ctx.get<ECS::Singletons::FreeflyingCameraSettings>();
    }

    i32 CameraHandler::GetPosition(Zenith* zenith)
    {
        ActiveCameraView view = GetActiveCamera();
        vec3 pos = view.transform ? view.transform->GetWorldPosition() : vec3(0.0f);
        zenith->Push(pos);
        return 1;
    }

    i32 CameraHandler::GetPitch(Zenith* zenith)
    {
        ActiveCameraView view = GetActiveCamera();
        zenith->Push(view.camera ? view.camera->pitch : 0.0f);
        return 1;
    }

    i32 CameraHandler::GetYaw(Zenith* zenith)
    {
        ActiveCameraView view = GetActiveCamera();
        zenith->Push(view.camera ? view.camera->yaw : 0.0f);
        return 1;
    }

    i32 CameraHandler::GetRoll(Zenith* zenith)
    {
        ActiveCameraView view = GetActiveCamera();
        zenith->Push(view.camera ? view.camera->roll : 0.0f);
        return 1;
    }

    i32 CameraHandler::GetForward(Zenith* zenith)
    {
        ActiveCameraView view = GetActiveCamera();
        vec3 forward = view.transform ? view.transform->GetLocalForward() : vec3(0.0f, 0.0f, 1.0f);
        zenith->Push(forward);
        return 1;
    }

    i32 CameraHandler::GetRight(Zenith* zenith)
    {
        ActiveCameraView view = GetActiveCamera();
        vec3 right = view.transform ? view.transform->GetLocalRight() : vec3(1.0f, 0.0f, 0.0f);
        zenith->Push(right);
        return 1;
    }

    i32 CameraHandler::GetUp(Zenith* zenith)
    {
        ActiveCameraView view = GetActiveCamera();
        vec3 up = view.transform ? view.transform->GetLocalUp() : vec3(0.0f, 1.0f, 0.0f);
        zenith->Push(up);
        return 1;
    }

    i32 CameraHandler::GetFOV(Zenith* zenith)
    {
        ActiveCameraView view = GetActiveCamera();
        zenith->Push(view.camera ? view.camera->fov : 0.0f);
        return 1;
    }

    i32 CameraHandler::GetNearClip(Zenith* zenith)
    {
        ActiveCameraView view = GetActiveCamera();
        zenith->Push(view.camera ? view.camera->nearClip : 0.0f);
        return 1;
    }

    i32 CameraHandler::GetFarClip(Zenith* zenith)
    {
        ActiveCameraView view = GetActiveCamera();
        zenith->Push(view.camera ? view.camera->farClip : 0.0f);
        return 1;
    }

    i32 CameraHandler::GetSpeed(Zenith* zenith)
    {
        ECS::Singletons::FreeflyingCameraSettings* settings = GetFreeflySettings();
        zenith->Push(settings ? settings->cameraSpeed : 0.0f);
        return 1;
    }

    i32 CameraHandler::SetSpeed(Zenith* zenith)
    {
        f32 speed = zenith->CheckVal<f32>(1);
        ECS::Singletons::FreeflyingCameraSettings* settings = GetFreeflySettings();
        if (settings)
            settings->cameraSpeed = glm::max(speed, 0.0f);
        return 0;
    }

    i32 CameraHandler::GetMapInfo(Zenith* zenith)
    {
        ActiveCameraView view = GetActiveCamera();
        vec3 worldPos = view.transform ? view.transform->GetWorldPosition() : vec3(0.0f);

        vec2 chunkGlobalPos = ::Util::Map::WorldPositionToChunkGlobalPos(worldPos);
        vec2 chunkPos = ::Util::Map::GetChunkIndicesFromAdtPosition(chunkGlobalPos);
        vec2 chunkRemainder = chunkPos - glm::floor(chunkPos);

        vec2 cellLocalPos = (chunkRemainder * Terrain::CHUNK_SIZE);
        vec2 cellPos = cellLocalPos / Terrain::CELL_SIZE;
        vec2 cellRemainder = cellPos - glm::floor(cellPos);

        vec2 patchLocalPos = (cellRemainder * Terrain::CELL_SIZE);
        vec2 patchPos = patchLocalPos / Terrain::PATCH_SIZE;

        u32 chunkID = ::Util::Map::GetChunkIdFromChunkPos(chunkPos);
        u32 cellID = ::Util::Map::GetCellIdFromCellPos(cellPos);
        u32 patchID = ::Util::Map::GetCellIdFromCellPos(patchPos);

        zenith->CreateTable();
        zenith->AddTableField("chunkID", chunkID);
        zenith->AddTableField("chunkX", chunkPos.x);
        zenith->AddTableField("chunkY", chunkPos.y);
        zenith->AddTableField("cellID", cellID);
        zenith->AddTableField("cellX", cellPos.x);
        zenith->AddTableField("cellY", cellPos.y);
        zenith->AddTableField("patchID", patchID);
        zenith->AddTableField("patchX", patchPos.x);
        zenith->AddTableField("patchY", patchPos.y);
        return 1;
    }

    static ClientDB::Data* GetCameraSaveStorage()
    {
        EnttRegistries* registries = ServiceLocator::GetEnttRegistries();
        if (!registries || !registries->dbRegistry)
            return nullptr;
        auto& ctx = registries->dbRegistry->ctx();
        if (!ctx.contains<ECS::Singletons::ClientDBSingleton>())
            return nullptr;
        auto& clientDBSingleton = ctx.get<ECS::Singletons::ClientDBSingleton>();
        return clientDBSingleton.Get(ClientDBHash::CameraSave);
    }

    static bool LoadCameraSaveByName(const std::string& name)
    {
        if (name.empty())
            return false;

        u32 hash = StringUtils::fnv1a_32(name.c_str(), name.length());
        ClientDB::Data* storage = GetCameraSaveStorage();
        if (!storage || !ECSUtil::Camera::HasCameraSave(hash))
            return false;

        u32 id = ECSUtil::Camera::GetCameraSaveID(hash);
        const auto& record = storage->Get<MetaGen::Shared::ClientDB::CameraSaveRecord>(id);
        const std::string& code = storage->GetString(record.code);
        return ECSUtil::Camera::LoadSaveLocationFromBase64(code);
    }

    i32 CameraHandler::GetSaveNames(Zenith* zenith)
    {
        zenith->CreateTable();

        ClientDB::Data* storage = GetCameraSaveStorage();
        if (!storage)
            return 1;

        std::vector<std::string> names;
        storage->Each([&names, storage](u32 /*id*/, const MetaGen::Shared::ClientDB::CameraSaveRecord& record) -> bool
        {
            names.push_back(storage->GetString(record.name));
            return true;
        });
        std::sort(names.begin(), names.end());

        for (size_t i = 0; i < names.size(); ++i)
        {
            zenith->AddTableField(static_cast<i32>(i + 1), names[i].c_str());
        }
        return 1;
    }

    i32 CameraHandler::HasSave(Zenith* zenith)
    {
        const char* nameRaw = zenith->CheckVal<const char*>(1);
        std::string name = nameRaw ? nameRaw : "";
        u32 hash = StringUtils::fnv1a_32(name.c_str(), name.length());
        zenith->Push(ECSUtil::Camera::HasCameraSave(hash));
        return 1;
    }

    i32 CameraHandler::LoadSave(Zenith* zenith)
    {
        const char* nameRaw = zenith->CheckVal<const char*>(1);
        std::string name = nameRaw ? nameRaw : "";
        zenith->Push(LoadCameraSaveByName(name));
        return 1;
    }

    i32 CameraHandler::AddSave(Zenith* zenith)
    {
        const char* nameRaw = zenith->CheckVal<const char*>(1);
        std::string name = nameRaw ? nameRaw : "";
        zenith->Push(ECSUtil::Camera::AddCameraSave(name));
        return 1;
    }

    i32 CameraHandler::DeleteSave(Zenith* zenith)
    {
        const char* nameRaw = zenith->CheckVal<const char*>(1);
        std::string name = nameRaw ? nameRaw : "";
        u32 hash = StringUtils::fnv1a_32(name.c_str(), name.length());
        zenith->Push(ECSUtil::Camera::RemoveCameraSave(hash));
        return 1;
    }

    i32 CameraHandler::GetStartupSave(Zenith* zenith)
    {
        const char* name = CVarSystem::Get()->GetStringCVar(CVarCategory::Client, "cameraStartupSave");
        zenith->Push(name ? name : "");
        return 1;
    }

    i32 CameraHandler::SetStartupSave(Zenith* zenith)
    {
        const char* nameRaw = zenith->IsString(1) ? zenith->Get<const char*>(1) : "";
        CVarSystem::Get()->SetStringCVar(CVarCategory::Client, "cameraStartupSave", nameRaw ? nameRaw : "");
        return 0;
    }

    void CameraHandler::Update(Zenith* zenith, f32 deltaTime)
    {
        // Auto-goto the configured startup save once, after the camera-save DB and active camera are
        // both ready (so the saved location actually applies).
        if (_appliedStartupSave)
            return;

        if (!GetCameraSaveStorage())
            return;

        ActiveCameraView view = GetActiveCamera();
        if (!view.transform)
            return;

        _appliedStartupSave = true;

        const char* startup = CVarSystem::Get()->GetStringCVar(CVarCategory::Client, "cameraStartupSave");
        std::string name = startup ? startup : "";
        if (!name.empty())
            LoadCameraSaveByName(name);
    }

    i32 CameraHandler::SetOnTransformChanged(Zenith* zenith)
    {
        CameraHandler* self = GetSelf();
        if (!self)
            return 0;

        Scripting::Util::Zenith::Unref(zenith, self->_onTransformChangedRef);
        self->_onTransformChangedRef = LUA_NOREF;

        if (zenith->IsFunction(1))
        {
            self->_onTransformChangedRef = zenith->GetRef(1);
        }
        return 0;
    }

    i32 CameraHandler::SetOnSavesChanged(Zenith* zenith)
    {
        CameraHandler* self = GetSelf();
        if (!self)
            return 0;

        Scripting::Util::Zenith::Unref(zenith, self->_onSavesChangedRef);
        self->_onSavesChangedRef = LUA_NOREF;

        if (zenith->IsFunction(1))
        {
            self->_onSavesChangedRef = zenith->GetRef(1);
        }
        return 0;
    }

    void CameraHandler::OnCameraTransformChanged(Zenith* zenith)
    {
        if (_onTransformChangedRef == LUA_NOREF)
            return;

        zenith->GetRawI(LUA_REGISTRYINDEX, _onTransformChangedRef);
        zenith->PCall(0);
    }

    void CameraHandler::OnCameraSavesChanged(Zenith* zenith)
    {
        if (_onSavesChangedRef == LUA_NOREF)
            return;

        zenith->GetRawI(LUA_REGISTRYINDEX, _onSavesChangedRef);
        zenith->PCall(0);
    }
}
