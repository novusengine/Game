#include "GlobalHandler.h"
#include "Game-Lib/Application/EnttRegistries.h"
#include "Game-Lib/ECS/Components/UnitEquipment.h"
#include "Game-Lib/ECS/Singletons/CharacterSingleton.h"
#include "Game-Lib/ECS/Singletons/NetworkState.h"
#include "Game-Lib/ECS/Singletons/Database/ClientDBSingleton.h"
#include "Game-Lib/ECS/Singletons/Database/MapSingleton.h"
#include "Game-Lib/ECS/Util/MessageBuilderUtil.h"
#include "Game-Lib/ECS/Util/Database/MapUtil.h"
#include "Game-Lib/Gameplay/MapLoader.h"
#include "Game-Lib/Gameplay/Attachment/Defines.h"
#include "Game-Lib/Gameplay/Database/Item.h"
#include "Game-Lib/Gameplay/GameConsole/GameConsole.h"
#include "Game-Lib/Gameplay/GameConsole/GameConsoleCommandHandler.h"
#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Scripting/LuaState.h"
#include "Game-Lib/Scripting/LuaManager.h"
#include "Game-Lib/Scripting/Database/Item.h"
#include "Game-Lib/Scripting/Systems/LuaSystemBase.h"
#include "Game-Lib/Util/ServiceLocator.h"
#include "Game-Lib/Util/UnitUtil.h"

#include <Meta/Generated/Shared/ClientDB.h>

#include <Network/Client.h>

#include <entt/entt.hpp>
#include <lualib.h>
#include <Game-Lib/ECS/Util/Network/NetworkUtil.h>
#include <Meta/Generated/Shared/NetworkPacket.h>

namespace Scripting
{
    void GlobalHandler::Register(lua_State* state)
    {
        LuaManager* luaManager = ServiceLocator::GetLuaManager();
        LuaState ctx(state);

        ctx.CreateTableAndPopulate("Engine", [&]()
        {
            ctx.SetTable("Name", "NovusEngine");
            ctx.SetTable("Version", vec3(0.0f, 0.0f, 1.0f));
        });

        LuaMethodTable::Set(state, globalMethods);
    }

    i32 GlobalHandler::AddCursor(lua_State* state)
    {
        LuaState ctx(state);

        const char* cursorName = ctx.Get(nullptr, 1);
        const char* cursorPath = ctx.Get(nullptr, 2);

        if (cursorName == nullptr || cursorPath == nullptr)
        {
            ctx.Push(false);
            return 1;
        }

        u32 hash = StringUtils::fnv1a_32(cursorName, strlen(cursorName));
        std::string path = cursorPath;

        GameRenderer* gameRenderer = ServiceLocator::GetGameRenderer();
        bool result = gameRenderer->AddCursor(hash, path);

        ctx.Push(result);
        return 1;
    }

    i32 GlobalHandler::SetCursor(lua_State* state)
    {
        LuaState ctx(state);

        const char* cursorName = ctx.Get(nullptr);
        if (cursorName == nullptr)
        {
            ctx.Push(false);
            return 1;
        }

        u32 hash = StringUtils::fnv1a_32(cursorName, strlen(cursorName));

        GameRenderer* gameRenderer = ServiceLocator::GetGameRenderer();
        bool result = gameRenderer->SetCursor(hash);

        ctx.Push(result);
        return 1;
    }

    i32 GlobalHandler::GetCurrentMap(lua_State* state)
    {
        LuaState ctx(state);

        const std::string& currentMapInternalName = ServiceLocator::GetGameRenderer()->GetTerrainLoader()->GetCurrentMapInternalName();

        ctx.Push(currentMapInternalName.c_str());
        return 1;
    }

    i32 GlobalHandler::LoadMap(lua_State* state)
    {
        LuaState ctx(state);

        const char* mapInternalName = ctx.Get(nullptr);
        size_t mapInternalNameLen = strlen(mapInternalName);

        if (mapInternalName == nullptr)
        {
            ctx.Push(false);
            return 1;
        }

        Generated::MapRecord* map = nullptr;
        if (!ECSUtil::Map::GetMapFromInternalName(mapInternalName, map))
        {
            ctx.Push(false);
            return 1;
        }

        u32 mapNameHash = StringUtils::fnv1a_32(mapInternalName, mapInternalNameLen);

        MapLoader* mapLoader = ServiceLocator::GetGameRenderer()->GetMapLoader();
        mapLoader->LoadMap(mapNameHash);

        ctx.Push(true);
        return 1;
    }

    i32 GlobalHandler::GetMapLoadingProgress(lua_State* state)
    {
        LuaState ctx(state);

        GameRenderer* gameRenderer = ServiceLocator::GetGameRenderer();
        f32 terrainProgress = gameRenderer->GetTerrainLoader()->GetLoadingProgress();
        f32 modelProgress = gameRenderer->GetModelLoader()->GetLoadingProgress();

        f32 totalProgress = (terrainProgress + modelProgress) / 2.0f;
        ctx.Push(totalProgress);
        return 1;
    }

    i32 GlobalHandler::EquipItem(lua_State* state)
    {
        LuaState ctx(state);

        u32 itemID = ctx.Get(0, 1);
        u32 slotIndex = ctx.Get(1, 2) - 1;

        if (itemID == 0 || slotIndex >= (u32)::Database::Item::ItemEquipSlot::Count)
        {
            ctx.Push(false);
            return 1;
        }

        entt::registry* dbRegistry = ServiceLocator::GetEnttRegistries()->dbRegistry;
        auto& clientDBSingleton = dbRegistry->ctx().get<ECS::Singletons::ClientDBSingleton>();
        auto* itemStorage = clientDBSingleton.Get(ClientDBHash::Item);

        if (!itemStorage->Has(itemID))
        {
            ctx.Push(false);
            return 1;
        }

        entt::registry* gameRegistry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& characterSingleton = gameRegistry->ctx().get<ECS::Singletons::CharacterSingleton>();

        auto& unitEquipment = gameRegistry->get<ECS::Components::UnitEquipment>(characterSingleton.moverEntity);

        u32 currentItemID = unitEquipment.equipmentSlotToItemID[slotIndex];
        if (currentItemID == itemID)
        {
            ctx.Push(true);
            return 1;
        }

        unitEquipment.equipmentSlotToItemID[slotIndex] = itemID;
        unitEquipment.dirtyItemIDSlots.insert((::Database::Item::ItemEquipSlot)slotIndex);
        gameRegistry->get_or_emplace<ECS::Components::UnitEquipmentDirty>(characterSingleton.moverEntity);

        ctx.Push(true);
        return 1;
    }

    i32 GlobalHandler::UnEquipItem(lua_State* state)
    {
        LuaState ctx(state);

        u32 slotIndex = ctx.Get(1, 1) - 1;

        if (slotIndex >= (u32)::Database::Item::ItemEquipSlot::Count)
        {
            ctx.Push(false);
            return 1;
        }

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& characterSingleton = registry->ctx().get<ECS::Singletons::CharacterSingleton>();

        auto& unitEquipment = registry->get<ECS::Components::UnitEquipment>(characterSingleton.moverEntity);

        u32 currentItemID = unitEquipment.equipmentSlotToItemID[slotIndex];
        if (currentItemID == 0)
        {
            ctx.Push(true);
            return 1;
        }

        unitEquipment.equipmentSlotToItemID[slotIndex] = 0;
        unitEquipment.dirtyItemIDSlots.insert((::Database::Item::ItemEquipSlot)slotIndex);
        registry->get_or_emplace<ECS::Components::UnitEquipmentDirty>(characterSingleton.moverEntity);

        ctx.Push(true);
        return 1;
    }

    i32 GlobalHandler::GetEquippedItem(lua_State* state)
    {
        LuaState ctx(state);

        u32 slotIndex = ctx.Get(1, 1) - 1;
        u32 itemID = 0;
        if (slotIndex >= 0 && slotIndex < (u32)::Database::Item::ItemEquipSlot::Count)
        {
            entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
            auto& characterSingleton = registry->ctx().get<ECS::Singletons::CharacterSingleton>();

            auto& unitEquipment = registry->get<ECS::Components::UnitEquipment>(characterSingleton.moverEntity);
            itemID = unitEquipment.equipmentSlotToItemID[slotIndex];
        }

        ctx.Push(itemID);
        return 1;
    }
    i32 GlobalHandler::ExecCmd(lua_State* state)
    {
        LuaState ctx(state);

        std::string command = ctx.Get("", 1);

        auto* gameConsole = ServiceLocator::GetGameConsole();
        auto* commandHandler = gameConsole->GetCommandHandler();
        bool wasProcessed = commandHandler->HandleCommand(gameConsole, command);

        ctx.Push(wasProcessed);
        return 1;
    }

    i32 GlobalHandler::SendChatMessage(lua_State* state)
    {
        LuaState ctx(state);

        std::string message = ctx.Get("", 1);
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
