#include "GameConsoleCommands.h"
#include "GameConsole.h"
#include "GameConsoleCommandHandler.h"
#include "Game-Lib/Application/EnttRegistries.h"
#include "Game-Lib/ECS/Components/Camera.h"
#include "Game-Lib/ECS/Components/CastInfo.h"
#include "Game-Lib/ECS/Components/NetworkedEntity.h"
#include "Game-Lib/ECS/Components/UnitStatsComponent.h"
#include "Game-Lib/ECS/Singletons/CharacterSingleton.h"
#include "Game-Lib/ECS/Singletons/ClientDBCollection.h"
#include "Game-Lib/ECS/Singletons/NetworkState.h"
#include "Game-Lib/ECS/Util/MessageBuilderUtil.h"
#include "Game-Lib/ECS/Util/Transforms.h"
#include "Game-Lib/Gameplay/MapLoader.h"
#include "Game-Lib/Scripting/LuaManager.h"
#include "Game-Lib/Util/CameraSaveUtil.h"
#include "Game-Lib/Util/ServiceLocator.h"
#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Rendering/Terrain/TerrainLoader.h"

#include <Base/Memory/Bytebuffer.h>

#include <Gameplay/GameDefine.h>
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

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
    ECS::Singletons::NetworkState& networkState = registry->ctx().get<ECS::Singletons::NetworkState>();

    std::shared_ptr<Bytebuffer> buffer = Bytebuffer::Borrow<128>();
    if (ECS::Util::MessageBuilder::Authentication::BuildConnectMessage(buffer, characterName))
    {
        networkState.client->Send(buffer);
    }

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
    auto& characterSingleton = registry->ctx().get<ECS::Singletons::CharacterSingleton>();
    auto& networkState = registry->ctx().get<ECS::Singletons::NetworkState>();
    auto& networkedEntity = registry->get<ECS::Components::NetworkedEntity>(characterSingleton.moverEntity);

    if (networkState.client && networkState.client->IsConnected())
    {
        std::shared_ptr<Bytebuffer> buffer = Bytebuffer::Borrow<128>();
        if (ECS::Util::MessageBuilder::Spell::BuildLocalRequestSpellCast(buffer))
        {
            networkState.client->Send(buffer);
        }
    }
    else
    {
        auto& castInfo = registry->emplace_or_replace<ECS::Components::CastInfo>(characterSingleton.moverEntity);
        castInfo.target = networkedEntity.targetEntity;
        castInfo.castTime = 1.0f;
        castInfo.duration = 0.0f;
    }

    return false;
}

bool GameConsoleCommands::HandleDamage(GameConsoleCommandHandler* commandHandler, GameConsole* gameConsole, std::vector<std::string>& subCommands)
{
    if (subCommands.size() == 0)
        return false;

    const std::string& damageAsString = subCommands[0];
    const u32 damage = std::stoi(damageAsString);

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
    ECS::Singletons::CharacterSingleton& characterSingleton = registry->ctx().get<ECS::Singletons::CharacterSingleton>();
    ECS::Singletons::NetworkState& networkState = registry->ctx().get<ECS::Singletons::NetworkState>();

    if (networkState.client && networkState.client->IsConnected())
    {
        std::shared_ptr<Bytebuffer> buffer = Bytebuffer::Borrow<128>();
        if (ECS::Util::MessageBuilder::Cheat::BuildCheatDamage(buffer, damage))
        {
            networkState.client->Send(buffer);
        }
    }
    else
    {
        auto& unitStatsComponent = registry->get<ECS::Components::UnitStatsComponent>(characterSingleton.moverEntity);
        unitStatsComponent.currentHealth = glm::max(unitStatsComponent.currentHealth - static_cast<f32>(damage), 0.0f);
    }

    return false;
}

bool GameConsoleCommands::HandleKill(GameConsoleCommandHandler* commandHandler, GameConsole* gameConsole, std::vector<std::string>& subCommands)
{
    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
    ECS::Singletons::CharacterSingleton& characterSingleton = registry->ctx().get<ECS::Singletons::CharacterSingleton>();
    ECS::Singletons::NetworkState& networkState = registry->ctx().get<ECS::Singletons::NetworkState>();

    if (networkState.client && networkState.client->IsConnected())
    {
        std::shared_ptr<Bytebuffer> buffer = Bytebuffer::Borrow<128>();
        if (ECS::Util::MessageBuilder::Cheat::BuildCheatKill(buffer))
        {
            networkState.client->Send(buffer);
        }
    }
    else
    {
        auto& unitStatsComponent = registry->get<ECS::Components::UnitStatsComponent>(characterSingleton.moverEntity);
        unitStatsComponent.currentHealth = 0.0f;
    }

    return false;
}

bool GameConsoleCommands::HandleRevive(GameConsoleCommandHandler* commandHandler, GameConsole* gameConsole, std::vector<std::string>& subCommands)
{
    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
    ECS::Singletons::CharacterSingleton& characterSingleton = registry->ctx().get<ECS::Singletons::CharacterSingleton>();
    ECS::Singletons::NetworkState& networkState = registry->ctx().get<ECS::Singletons::NetworkState>();

    if (networkState.client && networkState.client->IsConnected())
    {
        std::shared_ptr<Bytebuffer> buffer = Bytebuffer::Borrow<128>();
        if (ECS::Util::MessageBuilder::Cheat::BuildCheatResurrect(buffer))
        {
            networkState.client->Send(buffer);
        }
    }
    else
    {
        auto& unitStatsComponent = registry->get<ECS::Components::UnitStatsComponent>(characterSingleton.moverEntity);
        unitStatsComponent.currentHealth = unitStatsComponent.maxHealth;
    }

    return false;
}

bool GameConsoleCommands::HandleMorph(GameConsoleCommandHandler* commandHandler, GameConsole* gameConsole, std::vector<std::string>& subCommands)
{
    if (subCommands.size() == 0)
        return false;

    const std::string& morphIDAsString = subCommands[0];
    const u32 displayID = std::stoi(morphIDAsString);

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
    auto& clientDBCollection = registry->ctx().get<ECS::Singletons::ClientDBCollection>();
    auto* creatureDisplayStorage = clientDBCollection.Get<ClientDB::Definitions::CreatureDisplayInfo>(ECS::Singletons::ClientDBHash::CreatureDisplayInfo);

    if (!creatureDisplayStorage || !creatureDisplayStorage->HasRow(displayID))
        return false;

    ECS::Singletons::NetworkState& networkState = registry->ctx().get<ECS::Singletons::NetworkState>();

    if (networkState.client && networkState.client->IsConnected())
    {
        std::shared_ptr<Bytebuffer> buffer = Bytebuffer::Borrow<128>();
        if (ECS::Util::MessageBuilder::Cheat::BuildCheatMorph(buffer, displayID))
        {
            networkState.client->Send(buffer);
        }
    }
    else
    {
        auto& characterSingleton = registry->ctx().get<ECS::Singletons::CharacterSingleton>();
        ModelLoader* modelLoader = ServiceLocator::GetGameRenderer()->GetModelLoader();

        if (!modelLoader->LoadDisplayIDForEntity(characterSingleton.moverEntity, displayID))
            return false;

        gameConsole->PrintSuccess("Morphed into : %u", displayID);
    }

    return true;
}

bool GameConsoleCommands::HandleDemorph(GameConsoleCommandHandler* commandHandler, GameConsole* gameConsole, std::vector<std::string>& subCommands)
{
    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
    ECS::Singletons::NetworkState& networkState = registry->ctx().get<ECS::Singletons::NetworkState>();

    if (networkState.client && networkState.client->IsConnected())
    {
        std::shared_ptr<Bytebuffer> buffer = Bytebuffer::Borrow<128>();
        if (ECS::Util::MessageBuilder::Cheat::BuildCheatDemorph(buffer))
        {
            networkState.client->Send(buffer);
        }
    }
    else
    {
        gameConsole->PrintWarning("Failed to demorph, not connected");
    }

    return true;
}

bool GameConsoleCommands::HandleCreateChar(GameConsoleCommandHandler* commandHandler, GameConsole* gameConsole, std::vector<std::string>& subCommands)
{
    if (subCommands.size() == 0)
        return false;

    const std::string& characterName = subCommands[0];
    if (!StringUtils::StringIsAlphaAndAtLeastLength(characterName, 2))
    {
        gameConsole->PrintError("Failed to send Create Character, name supplied is invalid : %s", characterName.c_str());
        return true;
    }

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
    ECS::Singletons::NetworkState& networkState = registry->ctx().get<ECS::Singletons::NetworkState>();

    if (networkState.client && networkState.client->IsConnected())
    {
        std::shared_ptr<Bytebuffer> buffer = Bytebuffer::Borrow<128>();
        if (ECS::Util::MessageBuilder::Cheat::BuildCheatCreateChar(buffer, characterName))
        {
            networkState.client->Send(buffer);
        }
    }
    else
    {
        gameConsole->PrintWarning("Failed to send Create Character, not connected");
    }

    return true;
}

bool GameConsoleCommands::HandleDeleteChar(GameConsoleCommandHandler* commandHandler, GameConsole* gameConsole, std::vector<std::string>& subCommands)
{
    if (subCommands.size() == 0)
        return false;

    const std::string& characterName = subCommands[0];
    if (!StringUtils::StringIsAlphaAndAtLeastLength(characterName, 2))
    {
        gameConsole->PrintError("Failed to send Delete Character, name supplied is invalid : %s", characterName.c_str());
        return true;
    }

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
    ECS::Singletons::NetworkState& networkState = registry->ctx().get<ECS::Singletons::NetworkState>();

    if (networkState.client && networkState.client->IsConnected())
    {
        std::shared_ptr<Bytebuffer> buffer = Bytebuffer::Borrow<128>();
        if (ECS::Util::MessageBuilder::Cheat::BuildCheatDeleteChar(buffer, characterName))
        {
            networkState.client->Send(buffer);
        }
    }
    else
    {
        gameConsole->PrintWarning("Failed to send Delete Character, not connected");
    }

    return true;
}

bool GameConsoleCommands::HandleSetRace(GameConsoleCommandHandler* commandHandler, GameConsole* gameConsole, std::vector<std::string>& subCommands)
{
    if (subCommands.size() == 0)
        return false;

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
    ECS::Singletons::NetworkState& networkState = registry->ctx().get<ECS::Singletons::NetworkState>();

    if (!networkState.client || !networkState.client->IsConnected())
        return false;

    GameDefine::UnitRace race = GameDefine::UnitRace::None;
    std::string& raceName = subCommands[0];

    bool isSpecifiedAsID = std::isdigit(raceName[0]);
    if (isSpecifiedAsID)
    {
        race = static_cast<GameDefine::UnitRace>(raceName[0] - '0');
    }
    else
    {
        std::transform(raceName.begin(), raceName.end(), raceName.begin(), [](unsigned char c) { return std::tolower(c); });

        if (raceName == "human")
            race = GameDefine::UnitRace::Human;
        else if (raceName == "orc")
            race = GameDefine::UnitRace::Orc;
        else if (raceName == "dwarf")
            race = GameDefine::UnitRace::Dwarf;
        else if (raceName == "nightelf")
            race = GameDefine::UnitRace::NightElf;
        else if (raceName == "undead")
            race = GameDefine::UnitRace::Undead;
        else if (raceName == "tauren")
            race = GameDefine::UnitRace::Tauren;
        else if (raceName == "gnome")
            race = GameDefine::UnitRace::Gnome;
        else if (raceName == "troll")
            race = GameDefine::UnitRace::Troll;
    }

    if (race == GameDefine::UnitRace::None || race > GameDefine::UnitRace::Troll)
        return false;

    std::shared_ptr<Bytebuffer> buffer = Bytebuffer::Borrow<128>();
    if (ECS::Util::MessageBuilder::Cheat::BuildCheatSetRace(buffer, race))
    {
        networkState.client->Send(buffer);
    }

    return true;
}

bool GameConsoleCommands::HandleSetGender(GameConsoleCommandHandler* commandHandler, GameConsole* gameConsole, std::vector<std::string>& subCommands)
{
    if (subCommands.size() == 0)
        return false;

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
    ECS::Singletons::NetworkState& networkState = registry->ctx().get<ECS::Singletons::NetworkState>();

    if (!networkState.client || !networkState.client->IsConnected())
        return false;

    GameDefine::Gender gender = GameDefine::Gender::None;
    std::string& genderName = subCommands[0];

    bool isSpecifiedAsID = std::isdigit(genderName[0]);
    if (isSpecifiedAsID)
    {
        gender = static_cast<GameDefine::Gender>(genderName[0] - '0');
    }
    else
    {
        std::transform(genderName.begin(), genderName.end(), genderName.begin(), [](unsigned char c) { return std::tolower(c); });

        if (genderName == "male")
            gender = GameDefine::Gender::Male;
        else if (genderName == "female")
            gender = GameDefine::Gender::Female;
        else if (genderName == "other")
            gender = GameDefine::Gender::Other;
    }

    if (gender == GameDefine::Gender::None || gender > GameDefine::Gender::Other)
        return false;

    std::shared_ptr<Bytebuffer> buffer = Bytebuffer::Borrow<128>();
    if (ECS::Util::MessageBuilder::Cheat::BuildCheatSetGender(buffer, gender))
    {
        networkState.client->Send(buffer);
    }

    return true;
}

bool GameConsoleCommands::HandleSetClass(GameConsoleCommandHandler* commandHandler, GameConsole* gameConsole, std::vector<std::string>& subCommands)
{
    if (subCommands.size() == 0)
        return false;

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
    ECS::Singletons::NetworkState& networkState = registry->ctx().get<ECS::Singletons::NetworkState>();

    if (!networkState.client || !networkState.client->IsConnected())
        return false;

    GameDefine::UnitClass gameClass = GameDefine::UnitClass::None;
    std::string& gameClassName = subCommands[0];

    bool isSpecifiedAsID = std::isdigit(gameClassName[0]);
    if (isSpecifiedAsID)
    {
        gameClass = static_cast<GameDefine::UnitClass>(gameClassName[0] - '0');
    }
    else
    {
        std::transform(gameClassName.begin(), gameClassName.end(), gameClassName.begin(), [](unsigned char c) { return std::tolower(c); });

        if (gameClassName == "warrior")
            gameClass = GameDefine::UnitClass::Warrior;
        else if (gameClassName == "paladin")
            gameClass = GameDefine::UnitClass::Paladin;
        else if (gameClassName == "hunter")
            gameClass = GameDefine::UnitClass::Hunter;
        else if (gameClassName == "rogue")
            gameClass = GameDefine::UnitClass::Rogue;
        else if (gameClassName == "priest")
            gameClass = GameDefine::UnitClass::Priest;
        else if (gameClassName == "shaman")
            gameClass = GameDefine::UnitClass::Shaman;
        else if (gameClassName == "mage")
            gameClass = GameDefine::UnitClass::Mage;
        else if (gameClassName == "warlock")
            gameClass = GameDefine::UnitClass::Warlock;
        else if (gameClassName == "druid")
            gameClass = GameDefine::UnitClass::Druid;
    }

    if (gameClass == GameDefine::UnitClass::None || gameClass > GameDefine::UnitClass::Druid)
        return false;

    std::shared_ptr<Bytebuffer> buffer = Bytebuffer::Borrow<128>();
    if (ECS::Util::MessageBuilder::Cheat::BuildCheatSetClass(buffer, gameClass))
    {
        networkState.client->Send(buffer);
    }

    return true;
}

bool GameConsoleCommands::HandleSetLevel(GameConsoleCommandHandler* commandHandler, GameConsole* gameConsole, std::vector<std::string>& subCommands)
{
    if (subCommands.size() == 0)
        return false;

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
    ECS::Singletons::NetworkState& networkState = registry->ctx().get<ECS::Singletons::NetworkState>();

    if (!networkState.client || !networkState.client->IsConnected())
        return false;

    const std::string& levelAsString = subCommands[0];
    if (!StringUtils::StringIsNumeric(levelAsString))
        return false;

    u16 level = std::stoi(levelAsString);

    std::shared_ptr<Bytebuffer> buffer = Bytebuffer::Borrow<128>();
    if (ECS::Util::MessageBuilder::Cheat::BuildCheatSetLevel(buffer, level))
    {
        networkState.client->Send(buffer);
    }

    return true;
}
