#include "GlobalHandler.h"
#include "Game-Lib/Application/EnttRegistries.h"
#include "Game-Lib/ECS/Components/UnitEquipment.h"
#include "Game-Lib/ECS/Singletons/CharacterSingleton.h"
#include "Game-Lib/ECS/Singletons/NetworkState.h"
#include "Game-Lib/ECS/Singletons/Database/ClientDBSingleton.h"
#include "Game-Lib/ECS/Singletons/Database/MapSingleton.h"
#include "Game-Lib/ECS/Util/MessageBuilderUtil.h"
#include "Game-Lib/ECS/Util/Database/MapUtil.h"
#include <Game-Lib/ECS/Util/Network/NetworkUtil.h>
#include "Game-Lib/Gameplay/MapLoader.h"
#include "Game-Lib/Gameplay/Attachment/Defines.h"
#include "Game-Lib/Gameplay/Database/Item.h"
#include "Game-Lib/Gameplay/GameConsole/GameConsole.h"
#include "Game-Lib/Gameplay/GameConsole/GameConsoleCommandHandler.h"
#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Scripting/Database/Item.h"
#include "Game-Lib/Util/ServiceLocator.h"
#include "Game-Lib/Util/UnitUtil.h"

#include <Meta/Generated/Game/LuaEvent.h>
#include <Meta/Generated/Shared/ClientDB.h>
#include <Meta/Generated/Shared/NetworkPacket.h>

#include <Network/Client.h>

#include <Scripting/Zenith.h>

#include <entt/entt.hpp>
#include <lualib.h>

namespace Scripting
{
    void GlobalHandler::Register(Zenith* zenith)
    {
        LuaManager* luaManager = ServiceLocator::GetLuaManager();

        zenith->CreateTable("Engine");
        zenith->AddTableField("Name", "NovusEngine");
        zenith->AddTableField("Version", vec3(0.0f, 0.0f, 1.0f));
        zenith->Pop();

        LuaMethodTable::Set(zenith, globalMethods);
    }

    void GlobalHandler::PostLoad(Zenith* zenith)
    {
        const char* motd = CVarSystem::Get()->GetStringCVar(CVarCategory::Client, "scriptingMotd");
        zenith->CallEvent(Generated::LuaGameEventEnum::Loaded, Generated::LuaGameEventDataLoaded{
            .motd = motd
        });
    }

    void GlobalHandler::Update(Zenith* zenith, f32 deltaTime)
    {
        zenith->CallEvent(Generated::LuaGameEventEnum::Updated, Generated::LuaGameEventDataUpdated{
            .deltaTime = deltaTime
        });
    }

    i32 GlobalHandler::AddCursor(Zenith* zenith)
    {
        const char* cursorName = zenith->CheckVal<const char*>(1);
        const char* cursorPath = zenith->CheckVal<const char*>(2);

        if (cursorName == nullptr || cursorPath == nullptr)
        {
            zenith->Push(false);
            return 1;
        }

        u32 hash = StringUtils::fnv1a_32(cursorName, strlen(cursorName));
        std::string path = cursorPath;

        GameRenderer* gameRenderer = ServiceLocator::GetGameRenderer();
        bool result = gameRenderer->AddCursor(hash, path);

        zenith->Push(result);
        return 1;
    }

    i32 GlobalHandler::SetCursor(Zenith* zenith)
    {
        const char* cursorName = zenith->CheckVal<const char*>(1);
        if (cursorName == nullptr)
        {
            zenith->Push(false);
            return 1;
        }

        u32 hash = StringUtils::fnv1a_32(cursorName, strlen(cursorName));

        GameRenderer* gameRenderer = ServiceLocator::GetGameRenderer();
        bool result = gameRenderer->SetCursor(hash);

        zenith->Push(result);
        return 1;
    }

    i32 GlobalHandler::GetCurrentMap(Zenith* zenith)
    {
        const std::string& currentMapInternalName = ServiceLocator::GetGameRenderer()->GetTerrainLoader()->GetCurrentMapInternalName();
        zenith->Push(currentMapInternalName.c_str());

        return 1;
    }

    i32 GlobalHandler::LoadMap(Zenith* zenith)
    {
        const char* mapInternalName = zenith->CheckVal<const char*>(1);
        size_t mapInternalNameLen = strlen(mapInternalName);

        if (mapInternalName == nullptr)
        {
            zenith->Push(false);
            return 1;
        }

        Generated::MapRecord* map = nullptr;
        if (!ECSUtil::Map::GetMapFromInternalName(mapInternalName, map))
        {
            zenith->Push(false);
            return 1;
        }

        u32 mapNameHash = StringUtils::fnv1a_32(mapInternalName, mapInternalNameLen);

        MapLoader* mapLoader = ServiceLocator::GetGameRenderer()->GetMapLoader();
        mapLoader->LoadMap(mapNameHash);

        zenith->Push(true);
        return 1;
    }

    i32 GlobalHandler::GetMapLoadingProgress(Zenith* zenith)
    {
        GameRenderer* gameRenderer = ServiceLocator::GetGameRenderer();
        f32 terrainProgress = gameRenderer->GetTerrainLoader()->GetLoadingProgress();
        f32 modelProgress = gameRenderer->GetModelLoader()->GetLoadingProgress();

        f32 totalProgress = (terrainProgress + modelProgress) / 2.0f;
        zenith->Push(totalProgress);
        return 1;
    }

    i32 GlobalHandler::EquipItem(Zenith* zenith)
    {
        u32 itemID = zenith->CheckVal<u32>(1);
        u32 slotIndex = zenith->CheckVal<u32>(2) - 1;

        if (itemID == 0 || slotIndex >= (u32)::Database::Item::ItemEquipSlot::Count)
        {
            zenith->Push(false);
            return 1;
        }

        entt::registry* dbRegistry = ServiceLocator::GetEnttRegistries()->dbRegistry;
        auto& clientDBSingleton = dbRegistry->ctx().get<ECS::Singletons::ClientDBSingleton>();
        auto* itemStorage = clientDBSingleton.Get(ClientDBHash::Item);

        if (!itemStorage->Has(itemID))
        {
            zenith->Push(false);
            return 1;
        }

        entt::registry* gameRegistry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& characterSingleton = gameRegistry->ctx().get<ECS::Singletons::CharacterSingleton>();

        auto& unitEquipment = gameRegistry->get<ECS::Components::UnitEquipment>(characterSingleton.moverEntity);

        u32 currentItemID = unitEquipment.equipmentSlotToItemID[slotIndex];
        if (currentItemID == itemID)
        {
            zenith->Push(true);
            return 1;
        }

        unitEquipment.equipmentSlotToItemID[slotIndex] = itemID;
        unitEquipment.dirtyItemIDSlots.insert((::Database::Item::ItemEquipSlot)slotIndex);
        gameRegistry->get_or_emplace<ECS::Components::UnitEquipmentDirty>(characterSingleton.moverEntity);

        zenith->Push(true);
        return 1;
    }

    i32 GlobalHandler::UnEquipItem(Zenith* zenith)
    {
        u32 slotIndex = zenith->CheckVal<u32>(1) - 1;

        if (slotIndex >= (u32)::Database::Item::ItemEquipSlot::Count)
        {
            zenith->Push(false);
            return 1;
        }

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& characterSingleton = registry->ctx().get<ECS::Singletons::CharacterSingleton>();

        auto& unitEquipment = registry->get<ECS::Components::UnitEquipment>(characterSingleton.moverEntity);

        u32 currentItemID = unitEquipment.equipmentSlotToItemID[slotIndex];
        if (currentItemID == 0)
        {
            zenith->Push(true);
            return 1;
        }

        unitEquipment.equipmentSlotToItemID[slotIndex] = 0;
        unitEquipment.dirtyItemIDSlots.insert((::Database::Item::ItemEquipSlot)slotIndex);
        registry->get_or_emplace<ECS::Components::UnitEquipmentDirty>(characterSingleton.moverEntity);

        zenith->Push(true);
        return 1;
    }

    i32 GlobalHandler::GetEquippedItem(Zenith* zenith)
    {
        u32 slotIndex = zenith->CheckVal<u32>(1) - 1;
        u32 itemID = 0;
        if (slotIndex >= 0 && slotIndex < (u32)::Database::Item::ItemEquipSlot::Count)
        {
            entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
            auto& characterSingleton = registry->ctx().get<ECS::Singletons::CharacterSingleton>();

            auto& unitEquipment = registry->get<ECS::Components::UnitEquipment>(characterSingleton.moverEntity);
            itemID = unitEquipment.equipmentSlotToItemID[slotIndex];
        }

        zenith->Push(itemID);
        return 1;
    }
    i32 GlobalHandler::ExecCmd(Zenith* zenith)
    {
        std::string command = zenith->CheckVal<const char*>(1);

        auto* gameConsole = ServiceLocator::GetGameConsole();
        auto* commandHandler = gameConsole->GetCommandHandler();
        bool wasProcessed = commandHandler->HandleCommand(gameConsole, command);

        zenith->Push(wasProcessed);
        return 1;
    }

    i32 GlobalHandler::SendChatMessage(Zenith* zenith)
    {
        std::string message = zenith->CheckVal<const char*>(1);
        if (message.length() == 0 || message.length() >= 256)
            return 0;

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& networkState = registry->ctx().get<ECS::Singletons::NetworkState>();
        
        ECS::Util::Network::SendPacket(networkState, Generated::ClientSendChatMessagePacket{
            .message = message
        });

        return 0;
    }
}
