#include "GameConsoleCommands.h"
#include "GameConsole.h"
#include "GameConsoleCommandHandler.h"
#include "Game/Application/EnttRegistries.h"
#include "Game/ECS/Components/Camera.h"
#include "Game/ECS/Components/CastInfo.h"
#include "Game/ECS/Components/UnitStatsComponent.h"
#include "Game/ECS/Util/Transforms.h"
#include "Game/ECS/Singletons/CharacterSingleton.h"
#include "Game/ECS/Singletons/ClientDBCollection.h"
#include "Game/ECS/Singletons/NetworkState.h"
#include "Game/Gameplay/MapLoader.h"
#include "Game/Scripting/LuaManager.h"
#include "Game/Util/CameraSaveUtil.h"
#include "Game/Util/ServiceLocator.h"
#include "Game/Rendering/GameRenderer.h"
#include "Game/Rendering/Terrain/TerrainLoader.h"

#include <Base/Memory/Bytebuffer.h>

#include <Gameplay/Network/Opcode.h>

#include <Network/Client.h>
#include <Network/Define.h>

#include <base64/base64.h>
#include <entt/entt.hpp>

bool GameConsoleCommands::HandleHelp(GameConsoleCommandHandler* commandHandler, GameConsole* gameConsole, std::vector<std::string>& subCommands)
{
    gameConsole->Print("-- Help --");

    const auto& commandEntries = commandHandler->GetCommandEntries();
    u32 numCommands = static_cast<u32>(commandEntries.size());

    std::string commandList = "Available Commands : ";

    std::vector<const std::string*> commandNames;
    commandNames.reserve(numCommands);

    for (const auto& pair : commandEntries)
    {
        commandNames.push_back(&pair.second.name);
    }

    std::sort(commandNames.begin(), commandNames.end(), [&](const std::string* a, const std::string* b) { return *a < *b; });

    u32 counter = 0;
    for (const std::string* commandName : commandNames)
    {
        commandList += "'" + *commandName + "'";

        if (counter != numCommands - 1)
        {
            commandList += ", ";
        }

        counter++;
    }

    gameConsole->Print(commandList.c_str());
    return false;
}

bool GameConsoleCommands::HandlePing(GameConsoleCommandHandler* commandHandler, GameConsole* gameConsole, std::vector<std::string>& subCommands)
{
    gameConsole->Print("pong");
    return true;
}

bool GameConsoleCommands::HandleDoString(GameConsoleCommandHandler* commandHandler, GameConsole* gameConsole, std::vector<std::string>& subCommands)
{
    if (subCommands.size() == 0)
        return false;

    std::string code = "";

    for (u32 i = 0; i < subCommands.size(); i++)
    {
        if (i > 0)
        {
            code += " ";
        }

        code += subCommands[i];
    }

    ServiceLocator::GetLuaManager()->DoString(code);
    return true;
}

bool GameConsoleCommands::HandleLogin(GameConsoleCommandHandler* commandHandler, GameConsole* gameConsole, std::vector<std::string>& subCommands)
{
    if (subCommands.size() == 0)
        return false;

    std::string& characterName = subCommands[0];
    if (characterName.size() < 2)
        return false;

    std::shared_ptr<Bytebuffer> buffer = Bytebuffer::Borrow<128>();
    buffer->Put(Network::GameOpcode::Client_Connect);
    buffer->PutU16(static_cast<u16>(characterName.size()) + 1);
    buffer->PutString(characterName);

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;

    ECS::Singletons::NetworkState& networkState = registry->ctx().get<ECS::Singletons::NetworkState>();
    networkState.client->Send(buffer);

    return true;
}

bool GameConsoleCommands::HandleReloadScripts(GameConsoleCommandHandler* commandHandler, GameConsole* gameConsole, std::vector<std::string>& subCommands)
{
    Scripting::LuaManager* luaManager = ServiceLocator::GetLuaManager();
    luaManager->SetDirty();

    return false;
}

bool GameConsoleCommands::HandleSetCursor(GameConsoleCommandHandler* commandHandler, GameConsole* gameConsole, std::vector<std::string>& subCommands)
{
    if (subCommands.size() == 0)
        return false;

    std::string& cursorName = subCommands[0];
    u32 hash = StringUtils::fnv1a_32(cursorName.c_str(), cursorName.size());

    GameRenderer* gameRenderer = ServiceLocator::GetGameRenderer();
    gameRenderer->SetCursor(hash);

    return false;
}

bool GameConsoleCommands::HandleSaveCamera(GameConsoleCommandHandler* commandHandler, GameConsole* gameConsole, std::vector<std::string>& subCommands)
{
    if (subCommands.size() == 0)
        return false;

    std::string& cameraSaveName = subCommands[0];
    if (cameraSaveName.size() == 0)
        return false;

    std::string saveCode;
    if (Util::CameraSave::GenerateSaveLocation(cameraSaveName, saveCode))
    {
        gameConsole->PrintSuccess("Camera Save Code : %s : %s", cameraSaveName.c_str(), saveCode.c_str());
    }
    else
    {
        gameConsole->PrintError("Failed to generate Camera Save Code %s", cameraSaveName.c_str());
    }

    return false;
}

bool GameConsoleCommands::HandleLoadCamera(GameConsoleCommandHandler* commandHandler, GameConsole* gameConsole, std::vector<std::string>& subCommands)
{
    if (subCommands.size() == 0)
        return false;

    std::string& base64 = subCommands[0];

    if (base64.size() == 0)
        return false;

    if (Util::CameraSave::LoadSaveLocationFromBase64(base64))
    {
        gameConsole->PrintSuccess("Loaded Camera Code : %s", base64.c_str());
    }
    else
    {
        gameConsole->PrintError("Failed to load Camera Code %s", base64.c_str());
    }

    return false;
}

bool GameConsoleCommands::HandleClearMap(GameConsoleCommandHandler* commandHandler, GameConsole* gameConsole, std::vector<std::string>& subCommands)
{
    MapLoader* mapLoader = ServiceLocator::GetGameRenderer()->GetMapLoader();
    mapLoader->UnloadMap();

    return false;
}

bool GameConsoleCommands::HandleCast(GameConsoleCommandHandler* commandHandler, GameConsole* gameConsole, std::vector<std::string>& subCommands)
{
    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
    ECS::Singletons::CharacterSingleton& characterSingleton = registry->ctx().get<ECS::Singletons::CharacterSingleton>();
    ECS::Singletons::NetworkState& networkState = registry->ctx().get<ECS::Singletons::NetworkState>();

    if (networkState.client->IsConnected())
    {
        std::shared_ptr<Bytebuffer> buffer = Bytebuffer::Borrow<128>();
        Network::PacketHeader header =
        {
            .opcode = static_cast<Network::OpcodeType>(Network::GameOpcode::Client_LocalRequestSpellCast),
            .size = sizeof(u32)
        };

        buffer->Put(header);
        buffer->PutU32(0);

        networkState.client->Send(buffer);
    }
    else
    {
        auto& castInfo = registry->emplace_or_replace<ECS::Components::CastInfo>(characterSingleton.modelEntity);
        castInfo.target = characterSingleton.targetEntity;
        castInfo.castTime = 1.0f;
        castInfo.duration = 0.0f;
    }

    return false;
}

bool GameConsoleCommands::HandleDamage(GameConsoleCommandHandler* commandHandler, GameConsole* gameConsole, std::vector<std::string>& subCommands)
{
    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
    ECS::Singletons::CharacterSingleton& characterSingleton = registry->ctx().get<ECS::Singletons::CharacterSingleton>();
    ECS::Singletons::NetworkState& networkState = registry->ctx().get<ECS::Singletons::NetworkState>();

    if (networkState.client->IsConnected())
    {
        std::shared_ptr<Bytebuffer> buffer = Bytebuffer::Borrow<128>();
        Network::PacketHeader header =
        {
            .opcode = static_cast<Network::OpcodeType>(Network::GameOpcode::Client_SendCheatCommand),
            .size = sizeof(f32)
        };
        
        buffer->Put(header);
        buffer->PutF32(35);
        
        networkState.client->Send(buffer);
    }
    else
    {
        auto& unitStatsComponent = registry->get<ECS::Components::UnitStatsComponent>(characterSingleton.modelEntity);
        unitStatsComponent.currentHealth = glm::max(unitStatsComponent.currentHealth - 25.0f, 0.0f);
    }

    return false;
}

bool GameConsoleCommands::HandleKill(GameConsoleCommandHandler* commandHandler, GameConsole* gameConsole, std::vector<std::string>& subCommands)
{
    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
    ECS::Singletons::CharacterSingleton& characterSingleton = registry->ctx().get<ECS::Singletons::CharacterSingleton>();
    ECS::Singletons::NetworkState& networkState = registry->ctx().get<ECS::Singletons::NetworkState>();

    if (networkState.client->IsConnected())
    {
        std::shared_ptr<Bytebuffer> buffer = Bytebuffer::Borrow<128>();
        Network::PacketHeader header =
        {
            .opcode = static_cast<Network::OpcodeType>(Network::GameOpcode::Client_SendCheatCommand),
            .size = 0
        };
        
        buffer->Put(header);
        
        networkState.client->Send(buffer);
    }
    else
    {
        auto& unitStatsComponent = registry->get<ECS::Components::UnitStatsComponent>(characterSingleton.modelEntity);
        unitStatsComponent.currentHealth = 0.0f;
    }

    return false;
}

bool GameConsoleCommands::HandleRevive(GameConsoleCommandHandler* commandHandler, GameConsole* gameConsole, std::vector<std::string>& subCommands)
{
    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
    ECS::Singletons::CharacterSingleton& characterSingleton = registry->ctx().get<ECS::Singletons::CharacterSingleton>();
    ECS::Singletons::NetworkState& networkState = registry->ctx().get<ECS::Singletons::NetworkState>();

    if (networkState.client->IsConnected())
    {
        std::shared_ptr<Bytebuffer> buffer = Bytebuffer::Borrow<128>();
        Network::PacketHeader header =
        {
            .opcode = static_cast<Network::OpcodeType>(Network::GameOpcode::Client_SendCheatCommand),
            .size = 0
        };
        
        buffer->Put(header);
        
        networkState.client->Send(buffer);
    }
    else
    {
        auto& unitStatsComponent = registry->get<ECS::Components::UnitStatsComponent>(characterSingleton.modelEntity);
        unitStatsComponent.currentHealth = unitStatsComponent.maxHealth;
    }

    return false;
}

bool GameConsoleCommands::HandleMorph(GameConsoleCommandHandler* commandHandler, GameConsole* gameConsole, std::vector<std::string>& subCommands)
{
    if (subCommands.size() == 0)
        return false;

    const std::string morphIDAsString = subCommands[0];
    const u32 displayID = std::stoi(morphIDAsString);

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
    auto& clientDBCollection = registry->ctx().get<ECS::Singletons::ClientDBCollection>();
    auto* creatureDisplayStorage = clientDBCollection.Get<ClientDB::Definitions::CreatureDisplayInfo>(ECS::Singletons::ClientDBHash::CreatureDisplayInfo);

    if (!creatureDisplayStorage || !creatureDisplayStorage->HasRow(displayID))
        return false;

    ECS::Singletons::NetworkState& networkState = registry->ctx().get<ECS::Singletons::NetworkState>();

    if (networkState.client->IsConnected())
    {
        std::shared_ptr<Bytebuffer> buffer = Bytebuffer::Borrow<128>();
        Network::PacketHeader header =
        {
            .opcode = static_cast<Network::OpcodeType>(Network::GameOpcode::Client_SendCheatCommand),
            .size = 4
        };

        buffer->Put(header);
        buffer->Put(displayID);

        networkState.client->Send(buffer);
    }
    else
    {
        auto& characterSingleton = registry->ctx().get<ECS::Singletons::CharacterSingleton>();
        ModelLoader* modelLoader = ServiceLocator::GetGameRenderer()->GetModelLoader();

        if (!modelLoader->LoadDisplayIDForEntity(characterSingleton.modelEntity, displayID))
            return false;

        gameConsole->PrintSuccess("Morphed into : %u", displayID);
    }

    return true;
}

bool GameConsoleCommands::HandleCreateChar(GameConsoleCommandHandler* commandHandler, GameConsole* gameConsole, std::vector<std::string>& subCommands)
{
    if (subCommands.size() == 0)
        return false;

    const std::string characterName = subCommands[0];
    if (!StringUtils::StringIsAlphaAndAtLeastLength(characterName, 2))
    {
        gameConsole->PrintError("Failed to send Create Character, name supplied is invalid : %s", characterName.c_str());
        return true;
    }

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
    ECS::Singletons::NetworkState& networkState = registry->ctx().get<ECS::Singletons::NetworkState>();

    if (networkState.client->IsConnected())
    {
        std::shared_ptr<Bytebuffer> buffer = Bytebuffer::Borrow<128>();
        Network::PacketHeader header =
        {
            .opcode = static_cast<Network::OpcodeType>(Network::GameOpcode::Client_SendCheatCommand),
            .size = static_cast<u16>(characterName.size()) + 1u
        };

        buffer->Put(header);
        buffer->PutString(characterName);

        networkState.client->Send(buffer);
    }
    else
    {
        gameConsole->PrintWarning("Fialed to send Create Character, not connected");
    }

    return true;
}

bool GameConsoleCommands::HandleDeleteChar(GameConsoleCommandHandler* commandHandler, GameConsole* gameConsole, std::vector<std::string>& subCommands)
{
    if (subCommands.size() == 0)
        return false;

    const std::string characterName = subCommands[0];
    if (!StringUtils::StringIsAlphaAndAtLeastLength(characterName, 2))
    {
        gameConsole->PrintError("Failed to send Delete Character, name supplied is invalid : %s", characterName.c_str());
        return true;
    }

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
    ECS::Singletons::NetworkState& networkState = registry->ctx().get<ECS::Singletons::NetworkState>();

    if (networkState.client->IsConnected())
    {
        std::shared_ptr<Bytebuffer> buffer = Bytebuffer::Borrow<128>();
        Network::PacketHeader header =
        {
            .opcode = static_cast<Network::OpcodeType>(Network::GameOpcode::Client_SendCheatCommand),
            .size = static_cast<u16>(characterName.size()) + 1u
        };

        buffer->Put(header);
        buffer->PutString(characterName);

        networkState.client->Send(buffer);
    }
    else
    {
        gameConsole->PrintWarning("Fialed to send Delete Character, not connected");
    }

    return true;
}
