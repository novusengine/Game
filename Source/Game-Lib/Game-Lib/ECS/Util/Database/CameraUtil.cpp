#include "CameraUtil.h"

#include "Game-Lib/ECS/Singletons/Database/CameraSaveSingleton.h"
#include "Game-Lib/ECS/Singletons/Database/ClientDBSingleton.h"
#include "Game-Lib/Gameplay/Database/Shared.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <entt/entt.hpp>
#include <Game-Lib/Util/CameraSaveUtil.h>

namespace ECSUtil::Camera
{
    bool Refresh()
    {
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->dbRegistry;
        auto& ctx = registry->ctx();

        if (!ctx.find<ECS::Singletons::CameraSaveSingleton>())
            ctx.emplace<ECS::Singletons::CameraSaveSingleton>();

        auto& clientDBSingleton = ctx.get<ECS::Singletons::ClientDBSingleton>();

        if (!clientDBSingleton.Has(ClientDBHash::CameraSave))
        {
            clientDBSingleton.Register(ClientDBHash::CameraSave, "CameraSave");

            auto* cameraSaveStorage = clientDBSingleton.Get(ClientDBHash::CameraSave);
            cameraSaveStorage->Initialize({
                { "Name",   ClientDB::FieldType::StringRef },
                { "Code",   ClientDB::FieldType::StringRef }
            });

            ::Database::Shared::CameraSave cameraSave =
            {
                .name = cameraSaveStorage->AddString("Default"),
                .code = cameraSaveStorage->AddString("RGVmYXVsdAAAAAAAAAAAIEEAACDBAADwQQAAAAAAAAAAAACAPwAAgD8AAIA/AAAAAA==")
            };
            cameraSaveStorage->Replace(0, cameraSave);

            cameraSaveStorage->MarkDirty();
        }

        auto& cameraSaveSingleton = ctx.get<ECS::Singletons::CameraSaveSingleton>();
        auto* cameraSaveStorage = clientDBSingleton.Get(ClientDBHash::CameraSave);

        cameraSaveSingleton.cameraSaveNameHashToID.clear();

        u32 numRecords = cameraSaveStorage->GetNumRows();
        cameraSaveSingleton.cameraSaveNameHashToID.reserve(numRecords);

        cameraSaveStorage->Each([&cameraSaveSingleton, &cameraSaveStorage](u32 id, const ::Database::Shared::CameraSave& cameraSave) -> bool
        {
            const std::string& cameraSaveName = cameraSaveStorage->GetString(cameraSave.name);
            u32 nameHash = StringUtils::fnv1a_32(cameraSaveName.c_str(), cameraSaveName.length());

            cameraSaveSingleton.cameraSaveNameHashToID[nameHash] = id;
            return true;
        });

        return true;
    }

    bool HasCameraSave(u32 cameraNameHash)
    {
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->dbRegistry;
        auto& ctx = registry->ctx();

        auto& cameraSaveSingleton = ctx.get<ECS::Singletons::CameraSaveSingleton>();
        bool hasCamera = cameraSaveSingleton.cameraSaveNameHashToID.contains(cameraNameHash);

        return hasCamera;
    }
    u32 GetCameraSaveID(u32 cameraNameHash)
    {
        u32 cameraID = 0;
        if (HasCameraSave(cameraNameHash))
        {
            entt::registry* registry = ServiceLocator::GetEnttRegistries()->dbRegistry;
            auto& ctx = registry->ctx();
            auto& cameraSaveSingleton = ctx.get<ECS::Singletons::CameraSaveSingleton>();

            cameraID = cameraSaveSingleton.cameraSaveNameHashToID[cameraNameHash];
        }

        return cameraID;
    }
    bool RemoveCameraSave(u32 cameraNameHash)
    {
        if (!HasCameraSave(cameraNameHash))
            return false;

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->dbRegistry;
        auto& ctx = registry->ctx();

        auto& clientDBSingleton = ctx.get<ECS::Singletons::ClientDBSingleton>();
        auto& cameraSaveSingleton = ctx.get<ECS::Singletons::CameraSaveSingleton>();
        auto* cameraSaveStorage = clientDBSingleton.Get(ClientDBHash::CameraSave);

        u32 cameraSaveID = cameraSaveSingleton.cameraSaveNameHashToID[cameraNameHash];
        if (!cameraSaveStorage->Remove(cameraSaveID))
            return false;

        cameraSaveSingleton.cameraSaveNameHashToID.erase(cameraNameHash);

        cameraSaveStorage->MarkDirty();
        return true;
    }

    bool AddCameraSave(const std::string& cameraName)
    {
        if (cameraName.length() == 0)
            return false;

        u32 cameraNameHash = StringUtils::fnv1a_32(cameraName.c_str(), cameraName.length());
        if (HasCameraSave(cameraNameHash))
            return false;

        std::string saveCode;
        if (!GenerateSaveLocation(cameraName, saveCode))
            return false;

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->dbRegistry;
        auto& ctx = registry->ctx();

        auto& clientDBSingleton = ctx.get<ECS::Singletons::ClientDBSingleton>();
        auto& cameraSaveSingleton = ctx.get<ECS::Singletons::CameraSaveSingleton>();
        auto* cameraSaveStorage = clientDBSingleton.Get(ClientDBHash::CameraSave);

        ::Database::Shared::CameraSave cameraSave =
        {
            .name = cameraSaveStorage->AddString(cameraName),
            .code = cameraSaveStorage->AddString(saveCode)
        };

        u32 cameraID = cameraSaveStorage->Add(cameraSave);
        cameraSaveSingleton.cameraSaveNameHashToID[cameraNameHash] = cameraID;

        cameraSaveStorage->MarkDirty();
        return true;
    }

    bool GenerateSaveLocation(const std::string& saveName, std::string& result)
    {
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        entt::entity activeCamera = registry->ctx().get<ECS::Singletons::ActiveCamera>().entity;

        if (activeCamera == entt::null)
            return false;

        const std::string& mapInternalName = ServiceLocator::GetGameRenderer()->GetTerrainLoader()->GetCurrentMapInternalName();

        // SaveName, MapName, Position, Rotation, Scale
        u16 saveNameSize = static_cast<u16>(saveName.size()) + 1;
        u16 mapNameSize = static_cast<u16>(mapInternalName.size()) + 1;

        std::vector<u8> data(saveNameSize + mapNameSize + sizeof(vec3) + sizeof(quat) + sizeof(vec3));
        Bytebuffer buffer = Bytebuffer(data.data(), data.size());

        if (!buffer.PutString(saveName))
            return false;

        if (!buffer.PutString(mapInternalName))
            return false;

        {
            auto& camera = registry->get<ECS::Components::Camera>(activeCamera);
            auto& transform = registry->get<ECS::Components::Transform>(activeCamera);

            vec3 position = transform.GetWorldPosition();
            if (!buffer.Put(position))
                return false;

            if (!buffer.Put(camera.pitch))
                return false;

            if (!buffer.Put(camera.yaw))
                return false;

            if (!buffer.Put(camera.roll))
                return false;

            vec3 scale = transform.GetLocalScale();
            if (!buffer.Put(scale))
                return false;
        }

        std::string_view dataView = std::string_view(reinterpret_cast<char*>(data.data()), data.size());
        result = base64::to_base64(dataView);

        return true;
    }
    bool LoadSaveLocationFromBase64(const std::string& base64)
    {
        std::string result = base64::from_base64(base64);
        Bytebuffer buffer = Bytebuffer(result.data(), result.size());

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        entt::entity activeCamera = registry->ctx().get<ECS::Singletons::ActiveCamera>().entity;

        if (activeCamera == entt::null)
            return false;

        std::string cameraSaveName = "";
        std::string mapInternalName = "";

        if (!buffer.GetString(cameraSaveName))
            return false;

        if (!buffer.GetString(mapInternalName))
            return false;

        {
            auto& camera = registry->get<ECS::Components::Camera>(activeCamera);
            auto& transform = registry->get<ECS::Components::Transform>(activeCamera);

            vec3 position;
            if (!buffer.Get(position))
                return false;

            if (!buffer.Get(camera.pitch))
                return false;

            if (!buffer.Get(camera.yaw))
                return false;

            if (!buffer.Get(camera.roll))
                return false;

            vec3 scale;
            if (!buffer.Get(scale))
                return false;

            ECS::TransformSystem& tf = ECS::TransformSystem::Get(*registry);
            tf.SetWorldPosition(activeCamera, position);
            tf.SetLocalScale(activeCamera, scale);

            camera.dirtyView = true;
            camera.dirtyPerspective = true;
        }

        // Send LoadMap Request
        {
            MapLoader* mapLoader = ServiceLocator::GetGameRenderer()->GetMapLoader();

            if (mapInternalName.length() == 0)
            {
                mapLoader->UnloadMap();
            }
            else
            {
                u32 mapInternalNameHash = StringUtils::fnv1a_32(mapInternalName.c_str(), mapInternalName.length());
                mapLoader->LoadMap(mapInternalNameHash);
            }
        }

        return true;
    }
}
