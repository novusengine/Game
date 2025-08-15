#include "GameConsoleCommands.h"
#include "GameConsole.h"
#include "GameConsoleCommandHandler.h"
#include "Game-Lib/Application/EnttRegistries.h"
#include "Game-Lib/ECS/Components/Camera.h"
#include "Game-Lib/ECS/Components/CastInfo.h"
#include "Game-Lib/ECS/Components/Events.h"
#include "Game-Lib/ECS/Components/Model.h"
#include "Game-Lib/ECS/Components/Unit.h"
#include "Game-Lib/ECS/Components/UnitStatsComponent.h"
#include "Game-Lib/ECS/Singletons/CharacterSingleton.h"
#include "Game-Lib/ECS/Singletons/Database/ClientDBSingleton.h"
#include "Game-Lib/ECS/Singletons/NetworkState.h"
#include "Game-Lib/ECS/Singletons/UISingleton.h"
#include "Game-Lib/ECS/Util/EventUtil.h"
#include "Game-Lib/ECS/Util/MessageBuilderUtil.h"
#include "Game-Lib/ECS/Util/Transforms.h"
#include "Game-Lib/ECS/Util/UIUtil.h"
#include "Game-Lib/ECS/Util/Database/CameraUtil.h"
#include "Game-Lib/Gameplay/MapLoader.h"
#include "Game-Lib/Scripting/LuaManager.h"
#include "Game-Lib/Util/ServiceLocator.h"
#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Rendering/Terrain/TerrainLoader.h"

#include <Base/Memory/Bytebuffer.h>

#include <Gameplay/GameDefine.h>
#include <Gameplay/Network/Opcode.h>

#include <Meta/Generated/ClientDB.h>

#include <Network/Client.h>
#include <Network/Define.h>

#include <base64/base64.h>
#include <entt/entt.hpp>

//bool GameConsoleCommands::HandleLogin(GameConsoleCommandHandler* commandHandler, GameConsole* gameConsole, std::vector<std::string>& subCommands)
//{
//    if (subCommands.size() == 0)
//        return false;
//
//    std::string& characterName = subCommands[0];
//    if (characterName.size() < 2)
//        return false;
//
//    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
//    ECS::Singletons::NetworkState& networkState = registry->ctx().get<ECS::Singletons::NetworkState>();
//
//    std::shared_ptr<Bytebuffer> buffer = Bytebuffer::Borrow<128>();
//    if (ECS::Util::MessageBuilder::Authentication::BuildConnectMessage(buffer, characterName))
//    {
//        networkState.client->Send(buffer);
//    }
//
//    return true;
//}

//bool GameConsoleCommands::HandleCast(GameConsoleCommandHandler* commandHandler, GameConsole* gameConsole, std::vector<std::string>& subCommands)
//{
//    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
//    auto& characterSingleton = registry->ctx().get<ECS::Singletons::CharacterSingleton>();
//    auto& networkState = registry->ctx().get<ECS::Singletons::NetworkState>();
//    auto& unit = registry->get<ECS::Components::Unit>(characterSingleton.moverEntity);
//
//    if (networkState.client && networkState.client->IsConnected())
//    {
//        std::shared_ptr<Bytebuffer> buffer = Bytebuffer::Borrow<128>();
//        if (ECS::Util::MessageBuilder::Spell::BuildLocalRequestSpellCast(buffer))
//        {
//            networkState.client->Send(buffer);
//        }
//    }
//    else
//    {
//        auto& castInfo = registry->emplace_or_replace<ECS::Components::CastInfo>(characterSingleton.moverEntity);
//        castInfo.target = unit.targetEntity;
//        castInfo.castTime = 1.0f;
//        castInfo.duration = 0.0f;
//    }
//
//    return true;
//}

//bool GameConsoleCommands::HandleDamage(GameConsoleCommandHandler* commandHandler, GameConsole* gameConsole, std::vector<std::string>& subCommands)
//{
//    if (subCommands.size() == 0)
//        return false;
//
//    const std::string& damageAsString = subCommands[0];
//    const u32 damage = std::stoi(damageAsString);
//
//    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
//    ECS::Singletons::CharacterSingleton& characterSingleton = registry->ctx().get<ECS::Singletons::CharacterSingleton>();
//    ECS::Singletons::NetworkState& networkState = registry->ctx().get<ECS::Singletons::NetworkState>();
//
//    if (networkState.client && networkState.client->IsConnected())
//    {
//        std::shared_ptr<Bytebuffer> buffer = Bytebuffer::Borrow<128>();
//        if (ECS::Util::MessageBuilder::Cheat::BuildCheatDamage(buffer, damage))
//        {
//            networkState.client->Send(buffer);
//        }
//    }
//    else
//    {
//        auto& unitStatsComponent = registry->get<ECS::Components::UnitStatsComponent>(characterSingleton.moverEntity);
//        unitStatsComponent.currentHealth = glm::max(unitStatsComponent.currentHealth - static_cast<f32>(damage), 0.0f);
//    }
//
//    return true;
//}

//bool GameConsoleCommands::HandleKill(GameConsoleCommandHandler* commandHandler, GameConsole* gameConsole, std::vector<std::string>& subCommands)
//{
//    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
//    ECS::Singletons::CharacterSingleton& characterSingleton = registry->ctx().get<ECS::Singletons::CharacterSingleton>();
//    ECS::Singletons::NetworkState& networkState = registry->ctx().get<ECS::Singletons::NetworkState>();
//
//    if (networkState.client && networkState.client->IsConnected())
//    {
//        std::shared_ptr<Bytebuffer> buffer = Bytebuffer::Borrow<128>();
//        if (ECS::Util::MessageBuilder::Cheat::BuildCheatKill(buffer))
//        {
//            networkState.client->Send(buffer);
//        }
//    }
//    else
//    {
//        auto& unitStatsComponent = registry->get<ECS::Components::UnitStatsComponent>(characterSingleton.moverEntity);
//        unitStatsComponent.currentHealth = 0.0f;
//    }
//
//    return true;
//}

//bool GameConsoleCommands::HandleRevive(GameConsoleCommandHandler* commandHandler, GameConsole* gameConsole, std::vector<std::string>& subCommands)
//{
//    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
//    ECS::Singletons::CharacterSingleton& characterSingleton = registry->ctx().get<ECS::Singletons::CharacterSingleton>();
//    ECS::Singletons::NetworkState& networkState = registry->ctx().get<ECS::Singletons::NetworkState>();
//
//    if (networkState.client && networkState.client->IsConnected())
//    {
//        std::shared_ptr<Bytebuffer> buffer = Bytebuffer::Borrow<128>();
//        if (ECS::Util::MessageBuilder::Cheat::BuildCheatResurrect(buffer))
//        {
//            networkState.client->Send(buffer);
//        }
//    }
//    else
//    {
//        auto& unitStatsComponent = registry->get<ECS::Components::UnitStatsComponent>(characterSingleton.moverEntity);
//        unitStatsComponent.currentHealth = unitStatsComponent.maxHealth;
//    }
//
//    return true;
//}

//bool GameConsoleCommands::HandleCreateChar(GameConsoleCommandHandler* commandHandler, GameConsole* gameConsole, std::vector<std::string>& subCommands)
//{
//    if (subCommands.size() == 0)
//        return false;
//
//    const std::string& characterName = subCommands[0];
//    if (!StringUtils::StringIsAlphaAndAtLeastLength(characterName, 2))
//    {
//        gameConsole->PrintError("Failed to send Create Character, name supplied is invalid : %s", characterName.c_str());
//        return true;
//    }
//
//    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
//    ECS::Singletons::NetworkState& networkState = registry->ctx().get<ECS::Singletons::NetworkState>();
//
//    if (networkState.client && networkState.client->IsConnected())
//    {
//        std::shared_ptr<Bytebuffer> buffer = Bytebuffer::Borrow<128>();
//        if (ECS::Util::MessageBuilder::Cheat::BuildCheatCreateChar(buffer, characterName))
//        {
//            networkState.client->Send(buffer);
//        }
//    }
//    else
//    {
//        gameConsole->PrintWarning("Failed to send Create Character, not connected");
//    }
//
//    return true;
//}
//
//bool GameConsoleCommands::HandleDeleteChar(GameConsoleCommandHandler* commandHandler, GameConsole* gameConsole, std::vector<std::string>& subCommands)
//{
//    if (subCommands.size() == 0)
//        return false;
//
//    const std::string& characterName = subCommands[0];
//    if (!StringUtils::StringIsAlphaAndAtLeastLength(characterName, 2))
//    {
//        gameConsole->PrintError("Failed to send Delete Character, name supplied is invalid : %s", characterName.c_str());
//        return true;
//    }
//
//    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
//    ECS::Singletons::NetworkState& networkState = registry->ctx().get<ECS::Singletons::NetworkState>();
//
//    if (networkState.client && networkState.client->IsConnected())
//    {
//        std::shared_ptr<Bytebuffer> buffer = Bytebuffer::Borrow<128>();
//        if (ECS::Util::MessageBuilder::Cheat::BuildCheatDeleteChar(buffer, characterName))
//        {
//            networkState.client->Send(buffer);
//        }
//    }
//    else
//    {
//        gameConsole->PrintWarning("Failed to send Delete Character, not connected");
//    }
//
//    return true;
//}

//bool GameConsoleCommands::HandleSetClass(GameConsoleCommandHandler* commandHandler, GameConsole* gameConsole, std::vector<std::string>& subCommands)
//{
//    if (subCommands.size() == 0)
//        return false;
//
//    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
//    ECS::Singletons::NetworkState& networkState = registry->ctx().get<ECS::Singletons::NetworkState>();
//
//    if (!networkState.client || !networkState.client->IsConnected())
//        return false;
//
//    GameDefine::UnitClass gameClass = GameDefine::UnitClass::None;
//    std::string& gameClassName = subCommands[0];
//
//    bool isSpecifiedAsID = std::isdigit(gameClassName[0]);
//    if (isSpecifiedAsID)
//    {
//        gameClass = static_cast<GameDefine::UnitClass>(gameClassName[0] - '0');
//    }
//    else
//    {
//        std::transform(gameClassName.begin(), gameClassName.end(), gameClassName.begin(), [](unsigned char c) { return std::tolower(c); });
//
//        if (gameClassName == "warrior")
//            gameClass = GameDefine::UnitClass::Warrior;
//        else if (gameClassName == "paladin")
//            gameClass = GameDefine::UnitClass::Paladin;
//        else if (gameClassName == "hunter")
//            gameClass = GameDefine::UnitClass::Hunter;
//        else if (gameClassName == "rogue")
//            gameClass = GameDefine::UnitClass::Rogue;
//        else if (gameClassName == "priest")
//            gameClass = GameDefine::UnitClass::Priest;
//        else if (gameClassName == "shaman")
//            gameClass = GameDefine::UnitClass::Shaman;
//        else if (gameClassName == "mage")
//            gameClass = GameDefine::UnitClass::Mage;
//        else if (gameClassName == "warlock")
//            gameClass = GameDefine::UnitClass::Warlock;
//        else if (gameClassName == "druid")
//            gameClass = GameDefine::UnitClass::Druid;
//    }
//
//    if (gameClass == GameDefine::UnitClass::None || gameClass > GameDefine::UnitClass::Druid)
//        return false;
//
//    std::shared_ptr<Bytebuffer> buffer = Bytebuffer::Borrow<128>();
//    if (ECS::Util::MessageBuilder::Cheat::BuildCheatSetClass(buffer, gameClass))
//    {
//        networkState.client->Send(buffer);
//    }
//
//    return true;
//}

//bool GameConsoleCommands::HandleSetLevel(GameConsoleCommandHandler* commandHandler, GameConsole* gameConsole, std::vector<std::string>& subCommands)
//{
//    if (subCommands.size() == 0)
//        return false;
//
//    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
//    ECS::Singletons::NetworkState& networkState = registry->ctx().get<ECS::Singletons::NetworkState>();
//
//    if (!networkState.client || !networkState.client->IsConnected())
//        return false;
//
//    const std::string& levelAsString = subCommands[0];
//    if (!StringUtils::StringIsNumeric(levelAsString))
//        return false;
//
//    u16 level = std::stoi(levelAsString);
//
//    std::shared_ptr<Bytebuffer> buffer = Bytebuffer::Borrow<128>();
//    if (ECS::Util::MessageBuilder::Cheat::BuildCheatSetLevel(buffer, level))
//    {
//        networkState.client->Send(buffer);
//    }
//
//    return true;
//}

bool GameConsoleCommands::HandleHelp(GameConsole* gameConsole, Generated::HelpCommand& command)
{
    gameConsole->Print("-- Help --");

    const auto& commandEntries = gameConsole->GetCommandHandler()->GetCommandEntries();
    u32 numCommands = static_cast<u32>(commandEntries.size());

    std::string commandList = "Available Commands : ";

    std::vector<const std::string*> commandNames;
    commandNames.reserve(numCommands);

    for (const auto& pair : commandEntries)
    {
        commandNames.push_back(&pair.second.nameWithAliases);
    }

    std::sort(commandNames.begin(), commandNames.end(), [&](const std::string* a, const std::string* b) { return *a < *b; });

    u32 counter = 0;
    for (const std::string* commandName : commandNames)
    {
        commandList += *commandName;

        if (counter != numCommands - 1)
        {
            commandList += ", ";
        }

        counter++;
    }

    gameConsole->Print(commandList.c_str());
    return true;
}

bool GameConsoleCommands::HandlePing(GameConsole* gameConsole, Generated::PingCommand& command)
{
    gameConsole->Print("pong");

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
    auto& uiSingleton = registry->ctx().get<ECS::Singletons::UISingleton>();

    ECS::Util::UI::CallSendMessageToChat(uiSingleton.sendMessageToChatCallback, "System", "", "pong", false);
    return true;
}

bool GameConsoleCommands::HandleLua(GameConsole* gameConsole, Generated::LuaCommand& command)
{
    ServiceLocator::GetLuaManager()->DoString(command.code);
    return true;
}

bool GameConsoleCommands::HandleReloadScripts(GameConsole* gameConsole, Generated::ReloadScriptsCommand& command)
{
    Scripting::LuaManager* luaManager = ServiceLocator::GetLuaManager();
    luaManager->SetDirty();

    return true;
}

bool GameConsoleCommands::HandleRefreshDB(GameConsole* gameConsole, Generated::RefreshDBCommand& command)
{
    ECS::Util::EventUtil::PushEvent(ECS::Components::RefreshDatabaseEvent{});
    return true;
}

bool GameConsoleCommands::HandleSaveCamera(GameConsole* gameConsole, Generated::SaveCameraCommand& command)
{
    std::string saveCode;
    if (ECSUtil::Camera::GenerateSaveLocation(command.name, saveCode))
    {
        gameConsole->PrintSuccess("Camera Save Code : %s : %s", command.name.c_str(), saveCode.c_str());
    }
    else
    {
        gameConsole->PrintError("Failed to generate Camera Save Code %s", command.name.c_str());
    }

    return true;
}

bool GameConsoleCommands::HandleLoadCameraByCode(GameConsole* gameConsole, Generated::LoadCameraByCodeCommand& command)
{
    if (ECSUtil::Camera::LoadSaveLocationFromBase64(command.code))
    {
        gameConsole->PrintSuccess("Loaded Camera Code : %s", command.code.c_str());
    }
    else
    {
        gameConsole->PrintError("Failed to load Camera Code %s", command.code.c_str());
    }

    return true;
}

bool GameConsoleCommands::HandleClearMap(GameConsole* gameConsole, Generated::ClearMapCommand& command)
{
    MapLoader* mapLoader = ServiceLocator::GetGameRenderer()->GetMapLoader();
    mapLoader->UnloadMap();

    return true;
}

bool GameConsoleCommands::HandleMorph(GameConsole* gameConsole, Generated::MorphCommand& command)
{
    entt::registry* gameRegistry = ServiceLocator::GetEnttRegistries()->gameRegistry;
    entt::registry* dbRegistry = ServiceLocator::GetEnttRegistries()->dbRegistry;
    auto& clientDBSingleton = dbRegistry->ctx().get<ECS::Singletons::ClientDBSingleton>();
    auto* creatureDisplayStorage = clientDBSingleton.Get(ClientDBHash::CreatureDisplayInfo);

    if (!creatureDisplayStorage->Has(command.displayID))
        return false;

    ECS::Singletons::NetworkState& networkState = gameRegistry->ctx().get<ECS::Singletons::NetworkState>();

    if (networkState.client && networkState.client->IsConnected())
    {
        std::shared_ptr<Bytebuffer> buffer = Bytebuffer::Borrow<128>();
        if (ECS::Util::MessageBuilder::Cheat::BuildCheatMorph(buffer, command.displayID))
        {
            networkState.client->Send(buffer);
        }
    }
    else
    {
        auto& characterSingleton = gameRegistry->ctx().get<ECS::Singletons::CharacterSingleton>();
        ModelLoader* modelLoader = ServiceLocator::GetGameRenderer()->GetModelLoader();

        auto& model = gameRegistry->get<ECS::Components::Model>(characterSingleton.moverEntity);
        if (!modelLoader->LoadDisplayIDForEntity(characterSingleton.moverEntity, model, Database::Unit::DisplayInfoType::Creature, command.displayID))
            return false;

        gameConsole->PrintSuccess("Morphed into : %u", command.displayID);
    }

    return true;
}

bool GameConsoleCommands::HandleDemorph(GameConsole* gameConsole, Generated::DemorphCommand& command)
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

bool GameConsoleCommands::HandleCharacterCreate(GameConsole* gameConsole, Generated::CharacterCreateCommand& command)
{
    const std::string& characterName = command.name;
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

bool GameConsoleCommands::HandleCharacterDelete(GameConsole* gameConsole, Generated::CharacterDeleteCommand& command)
{
    const std::string& characterName = command.name;
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

bool GameConsoleCommands::HandleFly(GameConsole* gameConsole, Generated::FlyCommand& command)
{
    entt::registry* gameRegistry = ServiceLocator::GetEnttRegistries()->gameRegistry;
    auto& characterSingleton = gameRegistry->ctx().get<ECS::Singletons::CharacterSingleton>();
    if (characterSingleton.moverEntity == entt::null)
    {
        gameConsole->PrintError("Failed to handle flying command, character does not exist");
        return true;
    }

    auto& movementInfo = gameRegistry->get<ECS::Components::MovementInfo>(characterSingleton.moverEntity);
    movementInfo.movementFlags.flying = command.enable;

    return true;
}

bool GameConsoleCommands::HandleSetRace(GameConsole* gameConsole, Generated::SetRaceCommand& command)
{
    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
    ECS::Singletons::NetworkState& networkState = registry->ctx().get<ECS::Singletons::NetworkState>();

    if (!networkState.client || !networkState.client->IsConnected())
        return false;

    GameDefine::UnitRace race = GameDefine::UnitRace::None;
    std::string& raceName = command.race;

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

bool GameConsoleCommands::HandleSetGender(GameConsole* gameConsole, Generated::SetGenderCommand& command)
{
    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
    ECS::Singletons::NetworkState& networkState = registry->ctx().get<ECS::Singletons::NetworkState>();

    if (!networkState.client || !networkState.client->IsConnected())
        return false;

    GameDefine::UnitGender gender = GameDefine::UnitGender::None;
    std::string& genderName = command.gender;

    bool isSpecifiedAsID = std::isdigit(genderName[0]);
    if (isSpecifiedAsID)
    {
        gender = static_cast<GameDefine::UnitGender>(genderName[0] - '0');
    }
    else
    {
        std::transform(genderName.begin(), genderName.end(), genderName.begin(), [](unsigned char c) { return std::tolower(c); });

        if (genderName == "male")
            gender = GameDefine::UnitGender::Male;
        else if (genderName == "female")
            gender = GameDefine::UnitGender::Female;
        else if (genderName == "other")
            gender = GameDefine::UnitGender::Other;
    }

    if (gender == GameDefine::UnitGender::None || gender > GameDefine::UnitGender::Other)
        return false;

    std::shared_ptr<Bytebuffer> buffer = Bytebuffer::Borrow<128>();
    if (ECS::Util::MessageBuilder::Cheat::BuildCheatSetGender(buffer, gender))
    {
        networkState.client->Send(buffer);
    }

    return true;
}

bool GameConsoleCommands::HandleSyncItem(GameConsole* gameConsole, Generated::SyncItemCommand& command)
{
    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
    ECS::Singletons::NetworkState& networkState = registry->ctx().get<ECS::Singletons::NetworkState>();

    if (!networkState.client || !networkState.client->IsConnected())
        return false;

    entt::registry* dbRegistry = ServiceLocator::GetEnttRegistries()->dbRegistry;
    auto& clientDBSingleton = dbRegistry->ctx().get<ECS::Singletons::ClientDBSingleton>();
    auto* itemStorage = clientDBSingleton.Get(ClientDBHash::Item);

    if (!itemStorage->Has(command.itemID))
        return false;

    std::shared_ptr<Bytebuffer> buffer = Bytebuffer::Borrow<1024>();

    auto& item = itemStorage->Get<Generated::ItemRecord>(command.itemID);

    if (item.statTemplateID > 0)
    {
        auto* statTemplateStorage = clientDBSingleton.Get(ClientDBHash::ItemStatTemplate);
        if (statTemplateStorage->Has(item.statTemplateID))
        {
            auto& statTemplate = statTemplateStorage->Get<Generated::ItemStatTemplateRecord>(item.statTemplateID);
            if (!ECS::Util::MessageBuilder::Cheat::BuildCheatSetItemStatTemplate(buffer, statTemplateStorage, item.statTemplateID, statTemplate))
                return false;
        }
    }

    if (item.armorTemplateID > 0)
    {
        auto* armorTemplateStorage = clientDBSingleton.Get(ClientDBHash::ItemArmorTemplate);
        if (armorTemplateStorage->Has(item.armorTemplateID))
        {
            auto& armorTemplate = armorTemplateStorage->Get<Generated::ItemArmorTemplateRecord>(item.armorTemplateID);
            if (!ECS::Util::MessageBuilder::Cheat::BuildCheatSetItemArmorTemplate(buffer, armorTemplateStorage, item.armorTemplateID, armorTemplate))
                return false;
        }
    }

    if (item.weaponTemplateID > 0)
    {
        auto* weaponTemplateStorage = clientDBSingleton.Get(ClientDBHash::ItemWeaponTemplate);
        if (weaponTemplateStorage->Has(item.weaponTemplateID))
        {
            auto& weaponTemplate = weaponTemplateStorage->Get<Generated::ItemWeaponTemplateRecord>(item.weaponTemplateID);
            if (!ECS::Util::MessageBuilder::Cheat::BuildCheatSetItemWeaponTemplate(buffer, weaponTemplateStorage, item.weaponTemplateID, weaponTemplate))
                return false;
        }
    }
    
    if (item.shieldTemplateID > 0)
    {
        auto* shieldTemplateStorage = clientDBSingleton.Get(ClientDBHash::ItemShieldTemplate);
        if (shieldTemplateStorage->Has(item.shieldTemplateID))
        {
            auto& shieldTemplate = shieldTemplateStorage->Get<Generated::ItemShieldTemplateRecord>(item.shieldTemplateID);
            if (!ECS::Util::MessageBuilder::Cheat::BuildCheatSetItemShieldTemplate(buffer, shieldTemplateStorage, item.shieldTemplateID, shieldTemplate))
                return false;
        }
    }

    if (!ECS::Util::MessageBuilder::Cheat::BuildCheatSetItemTemplate(buffer, itemStorage, command.itemID, item))
        return false;

    networkState.client->Send(buffer);
    return true;
}

bool GameConsoleCommands::HandleForceSyncItems(GameConsole* gameConsole, Generated::ForceSyncItemsCommand& command)
{
    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
    ECS::Singletons::NetworkState& networkState = registry->ctx().get<ECS::Singletons::NetworkState>();

    if (!networkState.client || !networkState.client->IsConnected())
        return false;

    entt::registry* dbRegistry = ServiceLocator::GetEnttRegistries()->dbRegistry;
    auto& clientDBSingleton = dbRegistry->ctx().get<ECS::Singletons::ClientDBSingleton>();
    auto* itemStorage = clientDBSingleton.Get(ClientDBHash::Item);

    u32 numItems = itemStorage->GetNumRows();
    if (numItems == 0)
        return false;

    auto* statTemplateStorage = clientDBSingleton.Get(ClientDBHash::ItemStatTemplate);
    auto* armorTemplateStorage = clientDBSingleton.Get(ClientDBHash::ItemArmorTemplate);
    auto* weaponTemplateStorage = clientDBSingleton.Get(ClientDBHash::ItemWeaponTemplate);
    auto* shieldTemplateStorage = clientDBSingleton.Get(ClientDBHash::ItemShieldTemplate);

    std::shared_ptr<Bytebuffer> buffer = Bytebuffer::Borrow<1048576>();

    itemStorage->Each([&](u32 id, const Generated::ItemRecord& item)
    {
        if (item.statTemplateID > 0)
        {
            if (statTemplateStorage->Has(item.statTemplateID))
            {
                auto& statTemplate = statTemplateStorage->Get<Generated::ItemStatTemplateRecord>(item.statTemplateID);
                if (!ECS::Util::MessageBuilder::Cheat::BuildCheatSetItemStatTemplate(buffer, statTemplateStorage, item.statTemplateID, statTemplate))
                    return false;
            }
        }

        if (item.armorTemplateID > 0)
        {
            if (armorTemplateStorage->Has(item.armorTemplateID))
            {
                auto& armorTemplate = armorTemplateStorage->Get<Generated::ItemArmorTemplateRecord>(item.armorTemplateID);
                if (!ECS::Util::MessageBuilder::Cheat::BuildCheatSetItemArmorTemplate(buffer, armorTemplateStorage, item.armorTemplateID, armorTemplate))
                    return false;
            }
        }

        if (item.weaponTemplateID > 0)
        {
            if (weaponTemplateStorage->Has(item.weaponTemplateID))
            {
                auto& weaponTemplate = weaponTemplateStorage->Get<Generated::ItemWeaponTemplateRecord>(item.weaponTemplateID);
                if (!ECS::Util::MessageBuilder::Cheat::BuildCheatSetItemWeaponTemplate(buffer, weaponTemplateStorage, item.weaponTemplateID, weaponTemplate))
                    return false;
            }
        }

        if (item.shieldTemplateID > 0)
        {
            if (shieldTemplateStorage->Has(item.shieldTemplateID))
            {
                auto& shieldTemplate = shieldTemplateStorage->Get<Generated::ItemShieldTemplateRecord>(item.shieldTemplateID);
                if (!ECS::Util::MessageBuilder::Cheat::BuildCheatSetItemShieldTemplate(buffer, shieldTemplateStorage, item.shieldTemplateID, shieldTemplate))
                    return false;
            }
        }

        if (!ECS::Util::MessageBuilder::Cheat::BuildCheatSetItemTemplate(buffer, itemStorage, id, item))
            return false;

        return true;
    });

    if (buffer->writtenData == 0)
        return false;

    networkState.client->Send(buffer);
    return true;
}

bool GameConsoleCommands::HandleAddItem(GameConsole* gameConsole, Generated::AddItemCommand& command)
{
    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
    ECS::Singletons::NetworkState& networkState = registry->ctx().get<ECS::Singletons::NetworkState>();

    if (!networkState.client || !networkState.client->IsConnected())
        return false;

    entt::registry* dbRegistry = ServiceLocator::GetEnttRegistries()->dbRegistry;
    auto& clientDBSingleton = dbRegistry->ctx().get<ECS::Singletons::ClientDBSingleton>();
    auto* itemStorage = clientDBSingleton.Get(ClientDBHash::Item);

    if (!itemStorage->Has(command.itemID))
        return false;

    std::shared_ptr<Bytebuffer> buffer = Bytebuffer::Borrow<32>();

    if (!ECS::Util::MessageBuilder::Cheat::BuildCheatAddItem(buffer, command.itemID, 1u))
        return false;

    networkState.client->Send(buffer);
    return true;
}

bool GameConsoleCommands::HandleRemoveItem(GameConsole* gameConsole, Generated::RemoveItemCommand& command)
{
    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
    ECS::Singletons::NetworkState& networkState = registry->ctx().get<ECS::Singletons::NetworkState>();

    if (!networkState.client || !networkState.client->IsConnected())
        return false;

    entt::registry* dbRegistry = ServiceLocator::GetEnttRegistries()->dbRegistry;
    auto& clientDBSingleton = dbRegistry->ctx().get<ECS::Singletons::ClientDBSingleton>();
    auto* itemStorage = clientDBSingleton.Get(ClientDBHash::Item);

    if (!itemStorage->Has(command.itemID))
        return false;

    std::shared_ptr<Bytebuffer> buffer = Bytebuffer::Borrow<32>();

    if (!ECS::Util::MessageBuilder::Cheat::BuildCheatRemoveItem(buffer, command.itemID, 1u))
        return false;

    networkState.client->Send(buffer);
    return true;
}
