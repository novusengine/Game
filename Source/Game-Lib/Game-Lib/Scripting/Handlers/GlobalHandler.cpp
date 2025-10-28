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

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& networkState = registry->ctx().get<ECS::Singletons::NetworkState>();
        if (!networkState.isInWorld && networkState.authInfo.stage == ECS::AuthenticationStage::Completed)
        {
            zenith->CallEvent(Generated::LuaGameEventEnum::CharacterListChanged, Generated::LuaGameEventDataCharacterListChanged{});
        }
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
        unitEquipment.dirtyItemIDSlots.insert((Generated::ItemEquipSlotEnum)slotIndex);
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
        unitEquipment.dirtyItemIDSlots.insert((Generated::ItemEquipSlotEnum)slotIndex);
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

    i32 GlobalHandler::IsOfflineMode(Zenith* zenith)
    {
        CVarSystem* cvarSystem = CVarSystem::Get();
        bool isOffline = *cvarSystem->GetIntCVar(CVarCategory::Network, "offlineMode") != 0;

        zenith->Push(isOffline);
        return 1;
    }
    i32 GlobalHandler::IsOnline(Zenith* zenith)
    {
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& networkState = registry->ctx().get<ECS::Singletons::NetworkState>();

        bool isConnected = networkState.client->IsConnected();
        return 1;
    }
    i32 GlobalHandler::GetAuthStage(Zenith* zenith)
    {
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& networkState = registry->ctx().get<ECS::Singletons::NetworkState>();

        zenith->Push((i32)networkState.authInfo.stage);
        return 1;
    }
    i32 GlobalHandler::IsInWorld(Zenith* zenith)
    {
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& networkState = registry->ctx().get<ECS::Singletons::NetworkState>();

        bool isInWorld = networkState.isInWorld;
        zenith->Push(isInWorld);
        return 1;
    }
    i32 GlobalHandler::GetAccountName(Zenith* zenith)
    {
        const char* accountName = CVarSystem::Get()->GetStringCVar(CVarCategory::Network, "accountName");
        
        zenith->Push(accountName);
        return 1;
    }
    i32 GlobalHandler::Login(Zenith* zenith)
    {
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& networkState = registry->ctx().get<ECS::Singletons::NetworkState>();

        if (!networkState.client || networkState.client->IsConnected())
        {
            zenith->Push(false);
            return 1;
        }

        std::string username = zenith->CheckVal<const char*>(1);
        std::string password = zenith->CheckVal<const char*>(2);
        bool rememberMe = zenith->CheckVal<bool>(3);

        if (username.length() < 2 || username.length() >= 32 || password.length() < 2 || password.length() >= 128)
        {
            zenith->Push(false);
            return 1;
        }

        networkState.authInfo.username = username;
        networkState.authInfo.password = password;

        const char* connectIP = CVarSystem::Get()->GetStringCVar(CVarCategory::Network, "connectIP");
        if (networkState.client->Connect(connectIP, 4000))
        {
            if (rememberMe)
            {
                CVarSystem::Get()->SetStringCVar(CVarCategory::Network, "accountName", username.c_str());
            }
            else
            {
                CVarSystem::Get()->SetStringCVar(CVarCategory::Network, "accountName", "");
            }

            ECS::Util::Network::SendPacket(networkState, Generated::ConnectPacket{
                .accountName = username
            });
        }

        zenith->Push(true);
        return 1;
    }
    i32 GlobalHandler::Logout(Zenith* zenith)
    {
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& networkState = registry->ctx().get<ECS::Singletons::NetworkState>();

        if (!networkState.client || !networkState.client->IsConnected() || !networkState.isInWorld)
        {
            zenith->Push(false);
            return 1;
        }

        ECS::Util::Network::SendPacket(networkState, Generated::ClientCharacterLogoutPacket{});

        zenith->Push(true);
        return 1;
    }
    i32 GlobalHandler::Disconnect(Zenith* zenith)
    {
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& networkState = registry->ctx().get<ECS::Singletons::NetworkState>();

        if (!networkState.client || !networkState.client->IsConnected())
        {
            zenith->Push(false);
            return 1;
        }

        networkState.client->Stop();

        zenith->Push(true);
        return 1;
    }
    i32 GlobalHandler::GetCharacterList(Zenith* zenith)
    {
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& networkState = registry->ctx().get<ECS::Singletons::NetworkState>();

        u32 numCharacters = static_cast<u32>(networkState.characterListInfo.list.size());

        zenith->CreateTable();
        for (u32 i = 0; i < numCharacters; i++)
        {
            const ECS::CharacterListEntry& characterListEntry = networkState.characterListInfo.list[i];

            zenith->CreateTable();
            zenith->AddTableField("name", characterListEntry.name.c_str());
            zenith->AddTableField("race", characterListEntry.race);
            zenith->AddTableField("gender", characterListEntry.gender);
            zenith->AddTableField("class", characterListEntry.unitClass);
            zenith->AddTableField("level", characterListEntry.level);
            zenith->AddTableField("mapID", characterListEntry.mapID);

            zenith->SetTableKey(i + 1);
        }

        return 1;
    }
    i32 GlobalHandler::SelectCharacter(Zenith* zenith)
    {
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& networkState = registry->ctx().get<ECS::Singletons::NetworkState>();

        if (!networkState.client || !networkState.client->IsConnected() || networkState.characterListInfo.characterSelected)
        {
            zenith->Push(false);
            return 1;
        }

        u8 characterIndex = zenith->CheckVal<u8>(1) - 1;
        u32 numCharacters = static_cast<u32>(networkState.characterListInfo.list.size());

        if (characterIndex >= numCharacters)
        {
            zenith->Push(false);
            return 1;
        }

        networkState.characterListInfo.characterSelected = true;

        ECS::Util::Network::SendPacket(networkState, Generated::ClientCharacterSelectPacket{
            .characterIndex = characterIndex
        });

        zenith->Push(true);
        return 1;
    }
    i32 GlobalHandler::GetMapName(Zenith* zenith)
    {
        u16 mapID = zenith->CheckVal<u16>(1);

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->dbRegistry;
        auto& clientDBSingleton = registry->ctx().get<ECS::Singletons::ClientDBSingleton>();

        auto* mapStorage = clientDBSingleton.Get(ClientDBHash::Map);
        const auto& mapRecord = mapStorage->Get<Generated::MapRecord>(mapID);

        std::string mapName = mapStorage->GetString(mapRecord.name);
        zenith->Push(mapName.c_str());
        return 1;
    }
}
