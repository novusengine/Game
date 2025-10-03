#include "GameConsoleCommands.h"
#include "GameConsole.h"
#include "GameConsoleCommandHandler.h"
#include "Game-Lib/Application/EnttRegistries.h"
#include "Game-Lib/ECS/Components/Camera.h"
#include "Game-Lib/ECS/Components/CastInfo.h"
#include "Game-Lib/ECS/Components/Events.h"
#include "Game-Lib/ECS/Components/Model.h"
#include "Game-Lib/ECS/Components/Unit.h"
#include "Game-Lib/ECS/Components/UnitPowersComponent.h"
#include "Game-Lib/ECS/Singletons/CharacterSingleton.h"
#include "Game-Lib/ECS/Singletons/Database/ClientDBSingleton.h"
#include "Game-Lib/ECS/Singletons/Database/SpellSingleton.h"
#include "Game-Lib/ECS/Singletons/NetworkState.h"
#include "Game-Lib/ECS/Singletons/UISingleton.h"
#include "Game-Lib/ECS/Util/EventUtil.h"
#include "Game-Lib/ECS/Util/MessageBuilderUtil.h"
#include "Game-Lib/ECS/Util/Transforms.h"
#include "Game-Lib/ECS/Util/UIUtil.h"
#include "Game-Lib/ECS/Util/Database/CameraUtil.h"
#include "Game-Lib/ECS/Util/Database/SpellUtil.h"
#include "Game-Lib/ECS/Util/Network/NetworkUtil.h"
#include "Game-Lib/Gameplay/MapLoader.h"
#include "Game-Lib/Scripting/Util/ZenithUtil.h"
#include "Game-Lib/Util/ServiceLocator.h"
#include "Game-Lib/Util/UnitUtil.h"
#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Rendering/Terrain/TerrainLoader.h"

#include <Base/Memory/Bytebuffer.h>

#include <Gameplay/GameDefine.h>

#include <Meta/Generated/Game/Command.h>
#include <Meta/Generated/Shared/ClientDB.h>
#include <Meta/Generated/Shared/NetworkPacket.h>

#include <Network/Client.h>
#include <Network/Define.h>

#include <Scripting/LuaManager.h>
#include <Scripting/Zenith.h>

#include <base64/base64.h>
#include <entt/entt.hpp>

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
    Scripting::Zenith* zenith = Scripting::Util::Zenith::GetGlobal();
    ServiceLocator::GetLuaManager()->DoString(zenith, command.code);
    return true;
}

bool GameConsoleCommands::HandleScriptReload(GameConsole* gameConsole, Generated::ScriptReloadCommand& command)
{
    Scripting::LuaManager* luaManager = ServiceLocator::GetLuaManager();
    luaManager->SetDirty();

    return true;
}

bool GameConsoleCommands::HandleDatabaseReload(GameConsole* gameConsole, Generated::DatabaseReloadCommand& command)
{
    ECS::Util::EventUtil::PushEvent(ECS::Components::DatabaseReloadEvent{});
    return true;
}

bool GameConsoleCommands::HandleCameraSave(GameConsole* gameConsole, Generated::CameraSaveCommand& command)
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

bool GameConsoleCommands::HandleCameraLoadByCode(GameConsole* gameConsole, Generated::CameraLoadByCodeCommand& command)
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

bool GameConsoleCommands::HandleMapClear(GameConsole* gameConsole, Generated::MapClearCommand& command)
{
    MapLoader* mapLoader = ServiceLocator::GetGameRenderer()->GetMapLoader();
    mapLoader->UnloadMap();

    return true;
}

bool GameConsoleCommands::HandleUnitMorph(GameConsole* gameConsole, Generated::UnitMorphCommand& command)
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
        if (ECS::Util::MessageBuilder::Cheat::BuildCheatUnitMorph(buffer, command.displayID))
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

bool GameConsoleCommands::HandleUnitDemorph(GameConsole* gameConsole, Generated::UnitDemorphCommand& command)
{
    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
    ECS::Singletons::NetworkState& networkState = registry->ctx().get<ECS::Singletons::NetworkState>();

    if (networkState.client && networkState.client->IsConnected())
    {
        std::shared_ptr<Bytebuffer> buffer = Bytebuffer::Borrow<128>();
        if (ECS::Util::MessageBuilder::Cheat::BuildCheatUnitDemorph(buffer))
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

bool GameConsoleCommands::HandleCharacterAdd(GameConsole* gameConsole, Generated::CharacterAddCommand& command)
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
        if (ECS::Util::MessageBuilder::Cheat::BuildCheatCharacterAdd(buffer, characterName))
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

bool GameConsoleCommands::HandleCharacterRemove(GameConsole* gameConsole, Generated::CharacterRemoveCommand& command)
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
        if (ECS::Util::MessageBuilder::Cheat::BuildCheatCharacterRemove(buffer, characterName))
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

bool GameConsoleCommands::HandleCheatFly(GameConsole* gameConsole, Generated::CheatFlyCommand& command)
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

bool GameConsoleCommands::HandleUnitSetRace(GameConsole* gameConsole, Generated::UnitSetRaceCommand& command)
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
    if (ECS::Util::MessageBuilder::Cheat::BuildCheatUnitSetRace(buffer, race))
    {
        networkState.client->Send(buffer);
    }

    return true;
}

bool GameConsoleCommands::HandleUnitSetGender(GameConsole* gameConsole, Generated::UnitSetGenderCommand& command)
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
    if (ECS::Util::MessageBuilder::Cheat::BuildCheatUnitSetGender(buffer, gender))
    {
        networkState.client->Send(buffer);
    }

    return true;
}

bool GameConsoleCommands::HandleItemSync(GameConsole* gameConsole, Generated::ItemSyncCommand& command)
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
            if (!ECS::Util::MessageBuilder::Cheat::BuildCheatItemSetStatTemplate(buffer, statTemplateStorage, item.statTemplateID, statTemplate))
                return false;
        }
    }

    if (item.armorTemplateID > 0)
    {
        auto* armorTemplateStorage = clientDBSingleton.Get(ClientDBHash::ItemArmorTemplate);
        if (armorTemplateStorage->Has(item.armorTemplateID))
        {
            auto& armorTemplate = armorTemplateStorage->Get<Generated::ItemArmorTemplateRecord>(item.armorTemplateID);
            if (!ECS::Util::MessageBuilder::Cheat::BuildCheatItemSetArmorTemplate(buffer, armorTemplateStorage, item.armorTemplateID, armorTemplate))
                return false;
        }
    }

    if (item.weaponTemplateID > 0)
    {
        auto* weaponTemplateStorage = clientDBSingleton.Get(ClientDBHash::ItemWeaponTemplate);
        if (weaponTemplateStorage->Has(item.weaponTemplateID))
        {
            auto& weaponTemplate = weaponTemplateStorage->Get<Generated::ItemWeaponTemplateRecord>(item.weaponTemplateID);
            if (!ECS::Util::MessageBuilder::Cheat::BuildCheatItemSetWeaponTemplate(buffer, weaponTemplateStorage, item.weaponTemplateID, weaponTemplate))
                return false;
        }
    }

    if (item.shieldTemplateID > 0)
    {
        auto* shieldTemplateStorage = clientDBSingleton.Get(ClientDBHash::ItemShieldTemplate);
        if (shieldTemplateStorage->Has(item.shieldTemplateID))
        {
            auto& shieldTemplate = shieldTemplateStorage->Get<Generated::ItemShieldTemplateRecord>(item.shieldTemplateID);
            if (!ECS::Util::MessageBuilder::Cheat::BuildCheatItemSetShieldTemplate(buffer, shieldTemplateStorage, item.shieldTemplateID, shieldTemplate))
                return false;
        }
    }

    if (!ECS::Util::MessageBuilder::Cheat::BuildCheatItemSetTemplate(buffer, itemStorage, command.itemID, item))
        return false;

    networkState.client->Send(buffer);
    return true;
}

bool GameConsoleCommands::HandleItemSyncAll(GameConsole* gameConsole, Generated::ItemSyncAllCommand& command)
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
                if (!ECS::Util::MessageBuilder::Cheat::BuildCheatItemSetStatTemplate(buffer, statTemplateStorage, item.statTemplateID, statTemplate))
                    return false;
            }
        }

        if (item.armorTemplateID > 0)
        {
            if (armorTemplateStorage->Has(item.armorTemplateID))
            {
                auto& armorTemplate = armorTemplateStorage->Get<Generated::ItemArmorTemplateRecord>(item.armorTemplateID);
                if (!ECS::Util::MessageBuilder::Cheat::BuildCheatItemSetArmorTemplate(buffer, armorTemplateStorage, item.armorTemplateID, armorTemplate))
                    return false;
            }
        }

        if (item.weaponTemplateID > 0)
        {
            if (weaponTemplateStorage->Has(item.weaponTemplateID))
            {
                auto& weaponTemplate = weaponTemplateStorage->Get<Generated::ItemWeaponTemplateRecord>(item.weaponTemplateID);
                if (!ECS::Util::MessageBuilder::Cheat::BuildCheatItemSetWeaponTemplate(buffer, weaponTemplateStorage, item.weaponTemplateID, weaponTemplate))
                    return false;
            }
        }

        if (item.shieldTemplateID > 0)
        {
            if (shieldTemplateStorage->Has(item.shieldTemplateID))
            {
                auto& shieldTemplate = shieldTemplateStorage->Get<Generated::ItemShieldTemplateRecord>(item.shieldTemplateID);
                if (!ECS::Util::MessageBuilder::Cheat::BuildCheatItemSetShieldTemplate(buffer, shieldTemplateStorage, item.shieldTemplateID, shieldTemplate))
                    return false;
            }
        }

        if (!ECS::Util::MessageBuilder::Cheat::BuildCheatItemSetTemplate(buffer, itemStorage, id, item))
            return false;

        return true;
    });

    if (buffer->writtenData == 0)
        return false;

    networkState.client->Send(buffer);
    return true;
}

bool GameConsoleCommands::HandleItemAdd(GameConsole* gameConsole, Generated::ItemAddCommand& command)
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

    if (!ECS::Util::MessageBuilder::Cheat::BuildCheatItemAdd(buffer, command.itemID, 1u))
        return false;

    networkState.client->Send(buffer);
    return true;
}

bool GameConsoleCommands::HandleItemRemove(GameConsole* gameConsole, Generated::ItemRemoveCommand& command)
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

    if (!ECS::Util::MessageBuilder::Cheat::BuildCheatItemRemove(buffer, command.itemID, 1u))
        return false;

    networkState.client->Send(buffer);
    return true;
}

bool GameConsoleCommands::HandleCreatureAdd(GameConsole* gameConsole, Generated::CreatureAddCommand& command)
{
    entt::registry* gameRegistry = ServiceLocator::GetEnttRegistries()->gameRegistry;
    ECS::Singletons::NetworkState& networkState = gameRegistry->ctx().get<ECS::Singletons::NetworkState>();

    if (!networkState.client || !networkState.client->IsConnected())
        return false;

    std::shared_ptr<Bytebuffer> buffer = Bytebuffer::Borrow<32>();
    if (ECS::Util::MessageBuilder::Cheat::BuildCheatCreatureAdd(buffer, command.creatureTemplateID))
    {
        networkState.client->Send(buffer);
    }

    return true;
}

bool GameConsoleCommands::HandleCreatureRemove(GameConsole* gameConsole, Generated::CreatureRemoveCommand& command)
{
    entt::registry* gameRegistry = ServiceLocator::GetEnttRegistries()->gameRegistry;
    ECS::Singletons::NetworkState& networkState = gameRegistry->ctx().get<ECS::Singletons::NetworkState>();

    if (!networkState.client || !networkState.client->IsConnected())
        return false;

    auto& characterSingleton = gameRegistry->ctx().get<ECS::Singletons::CharacterSingleton>();
    auto& unitInfo = gameRegistry->get<ECS::Components::Unit>(characterSingleton.moverEntity);

    if (unitInfo.targetEntity == entt::null)
    {
        gameConsole->PrintError("Failed to remove creature, no target selected");
        return true;
    }

    if (!networkState.entityToNetworkID.contains(unitInfo.targetEntity))
    {
        gameConsole->PrintError("Failed to remove creature, target is not a networked entity");
        return true;
    }

    ObjectGUID creatureNetworkID = networkState.entityToNetworkID[unitInfo.targetEntity];
    if (creatureNetworkID.GetType() != ObjectGUID::Type::Creature)
    {
        gameConsole->PrintError("Failed to remove creature, target is not a creature");
        return true;
    }

    std::shared_ptr<Bytebuffer> buffer = Bytebuffer::Borrow<32>();
    if (ECS::Util::MessageBuilder::Cheat::BuildCheatCreatureRemove(buffer, creatureNetworkID))
    {
        networkState.client->Send(buffer);
    }

    return true;
}

bool GameConsoleCommands::HandleCreatureInfo(GameConsole* gameConsole, Generated::CreatureInfoCommand& command)
{
    entt::registry* gameRegistry = ServiceLocator::GetEnttRegistries()->gameRegistry;
    ECS::Singletons::NetworkState& networkState = gameRegistry->ctx().get<ECS::Singletons::NetworkState>();

    if (!networkState.client || !networkState.client->IsConnected())
        return false;

    auto& characterSingleton = gameRegistry->ctx().get<ECS::Singletons::CharacterSingleton>();
    auto& unitInfo = gameRegistry->get<ECS::Components::Unit>(characterSingleton.moverEntity);

    if (unitInfo.targetEntity == entt::null)
    {
        gameConsole->PrintError("Failed to get creature info, no target selected");
        return true;
    }

    if (!networkState.entityToNetworkID.contains(unitInfo.targetEntity))
    {
        gameConsole->PrintError("Failed to get creature info, target is not a networked entity");
        return true;
    }

    ObjectGUID creatureNetworkID = networkState.entityToNetworkID[unitInfo.targetEntity];
    if (creatureNetworkID.GetType() != ObjectGUID::Type::Creature)
    {
        gameConsole->PrintError("Failed to get creature info, target is not a creature");
        return true;
    }

    std::shared_ptr<Bytebuffer> buffer = Bytebuffer::Borrow<32>();
    if (ECS::Util::MessageBuilder::Cheat::BuildCheatCreatureInfo(buffer, creatureNetworkID))
    {
        networkState.client->Send(buffer);
    }

    return true;
}

bool GameConsoleCommands::HandleCheatLogin(GameConsole* gameConsole, Generated::CheatLoginCommand& command)
{
    if (command.characterName.size() < 2)
        return false;

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
    ECS::Singletons::NetworkState& networkState = registry->ctx().get<ECS::Singletons::NetworkState>();

    if (!networkState.client || networkState.client->IsConnected())
        return false;

    const char* connectIP = CVarSystem::Get()->GetStringCVar(CVarCategory::Network, "connectIP");
    if (networkState.client->Connect(connectIP, 4000))
    {
        ECS::Util::Network::SendPacket(networkState, Generated::ConnectPacket{
            .characterName = command.characterName
        });
    }

    return true;
}

bool GameConsoleCommands::HandleCheatDamage(GameConsole* gameConsole, Generated::CheatDamageCommand& command)
{
    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
    ECS::Singletons::CharacterSingleton& characterSingleton = registry->ctx().get<ECS::Singletons::CharacterSingleton>();
    ECS::Singletons::NetworkState& networkState = registry->ctx().get<ECS::Singletons::NetworkState>();

    if (networkState.client && networkState.client->IsConnected())
    {
        std::shared_ptr<Bytebuffer> buffer = Bytebuffer::Borrow<128>();
        if (ECS::Util::MessageBuilder::Cheat::BuildCheatDamage(buffer, command.amount))
        {
            networkState.client->Send(buffer);
        }
    }
    else
    {
        auto& unitPowersComponent = registry->get<ECS::Components::UnitPowersComponent>(characterSingleton.moverEntity);
        auto& healthPower = ::Util::Unit::GetPower(unitPowersComponent, Generated::PowerTypeEnum::Health);

        healthPower.current = glm::max(healthPower.current - static_cast<f64>(command.amount), 0.0);
        unitPowersComponent.dirtyPowerTypes.insert(Generated::PowerTypeEnum::Health);
    }

    return true;
}

bool GameConsoleCommands::HandleCheatKill(GameConsole* gameConsole, Generated::CheatKillCommand& command)
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
        auto& unitPowersComponent = registry->get<ECS::Components::UnitPowersComponent>(characterSingleton.moverEntity);
        auto& healthPower = ::Util::Unit::GetPower(unitPowersComponent, Generated::PowerTypeEnum::Health);

        healthPower.current = 0.0;
        unitPowersComponent.dirtyPowerTypes.insert(Generated::PowerTypeEnum::Health);
    }

    return true;
}

bool GameConsoleCommands::HandleCheatResurrect(GameConsole* gameConsole, Generated::CheatResurrectCommand& command)
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
        auto& unitPowersComponent = registry->get<ECS::Components::UnitPowersComponent>(characterSingleton.moverEntity);
        auto& healthPower = ::Util::Unit::GetPower(unitPowersComponent, Generated::PowerTypeEnum::Health);

        healthPower.current =   healthPower.max;
        unitPowersComponent.dirtyPowerTypes.insert(Generated::PowerTypeEnum::Health);
    }

    return true;
}

bool GameConsoleCommands::HandleCheatCast(GameConsole* gameConsole, Generated::CheatCastCommand& command)
{
    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
    auto& characterSingleton = registry->ctx().get<ECS::Singletons::CharacterSingleton>();
    auto& networkState = registry->ctx().get<ECS::Singletons::NetworkState>();

    if (ECS::Util::Network::IsConnected(networkState))
    {
        ECS::Util::Network::SendPacket(networkState, Generated::ClientSpellCastPacket{
            .spellID = command.spellID
        });
    }
    else
    {
        auto& unit = registry->get<ECS::Components::Unit>(characterSingleton.moverEntity);
        auto& castInfo = registry->emplace_or_replace<ECS::Components::CastInfo>(characterSingleton.moverEntity);
        castInfo.target = unit.targetEntity;
        castInfo.castTime = 1.0f;
        castInfo.timeToCast = 1.0f;
    }

    return true;
}

bool GameConsoleCommands::HandleMapSync(GameConsole* gameConsole, Generated::MapSyncCommand& command)
{
    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
    ECS::Singletons::NetworkState& networkState = registry->ctx().get<ECS::Singletons::NetworkState>();

    if (!ECS::Util::Network::IsConnected(networkState))
        return false;

    entt::registry* dbRegistry = ServiceLocator::GetEnttRegistries()->dbRegistry;
    auto& clientDBSingleton = dbRegistry->ctx().get<ECS::Singletons::ClientDBSingleton>();
    auto* mapStorage = clientDBSingleton.Get(ClientDBHash::Map);

    if (!mapStorage->Has(command.mapID))
        return false;

    std::shared_ptr<Bytebuffer> buffer = Bytebuffer::Borrow<1024>();

    auto& map = mapStorage->Get<Generated::MapRecord>(command.mapID);
    if (!ECS::Util::MessageBuilder::Cheat::BuildCheatMapAdd(buffer, mapStorage, command.mapID, map))
        return false;

    networkState.client->Send(buffer);
    return true;
}

bool GameConsoleCommands::HandleMapSyncAll(GameConsole* gameConsole, Generated::MapSyncAllCommand& command)
{
    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
    ECS::Singletons::NetworkState& networkState = registry->ctx().get<ECS::Singletons::NetworkState>();

    if (!ECS::Util::Network::IsConnected(networkState))
        return false;

    entt::registry* dbRegistry = ServiceLocator::GetEnttRegistries()->dbRegistry;
    auto& clientDBSingleton = dbRegistry->ctx().get<ECS::Singletons::ClientDBSingleton>();
    auto* mapStorage = clientDBSingleton.Get(ClientDBHash::Map);

    std::shared_ptr<Bytebuffer> buffer = Bytebuffer::Borrow<1048576>();

    mapStorage->Each([&](u32 id, const Generated::MapRecord& map)
    {
        if (!ECS::Util::MessageBuilder::Cheat::BuildCheatMapAdd(buffer, mapStorage, id, map))
            return false;

        return true;
    });

    networkState.client->Send(buffer);
    return true;
}

bool GameConsoleCommands::HandleGotoAdd(GameConsole* gameConsole, Generated::GotoAddCommand& command)
{
    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
    ECS::Singletons::NetworkState& networkState = registry->ctx().get<ECS::Singletons::NetworkState>();

    if (!ECS::Util::Network::IsConnected(networkState))
        return false;

    std::shared_ptr<Bytebuffer> buffer = Bytebuffer::Borrow<128>();
    if (ECS::Util::MessageBuilder::Cheat::BuildCheatGotoAdd(buffer, command))
    {
        networkState.client->Send(buffer);
    }

    return true;
}

bool GameConsoleCommands::HandleGotoAddHere(GameConsole* gameConsole, Generated::GotoAddHereCommand& command)
{
    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
    ECS::Singletons::NetworkState& networkState = registry->ctx().get<ECS::Singletons::NetworkState>();

    if (!ECS::Util::Network::IsConnected(networkState))
        return false;

    std::shared_ptr<Bytebuffer> buffer = Bytebuffer::Borrow<128>();
    if (ECS::Util::MessageBuilder::Cheat::BuildCheatGotoAddHere(buffer, command))
    {
        networkState.client->Send(buffer);
    }

    return true;
}

bool GameConsoleCommands::HandleGotoRemove(GameConsole* gameConsole, Generated::GotoRemoveCommand& command)
{
    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
    ECS::Singletons::NetworkState& networkState = registry->ctx().get<ECS::Singletons::NetworkState>();

    if (!ECS::Util::Network::IsConnected(networkState))
        return false;

    std::shared_ptr<Bytebuffer> buffer = Bytebuffer::Borrow<128>();
    if (ECS::Util::MessageBuilder::Cheat::BuildCheatGotoRemove(buffer, command))
    {
        networkState.client->Send(buffer);
    }

    return true;
}

bool GameConsoleCommands::HandleGotoMap(GameConsole* gameConsole, Generated::GotoMapCommand& command)
{
    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
    ECS::Singletons::NetworkState& networkState = registry->ctx().get<ECS::Singletons::NetworkState>();

    if (!ECS::Util::Network::IsConnected(networkState))
        return false;

    std::shared_ptr<Bytebuffer> buffer = Bytebuffer::Borrow<128>();
    if (ECS::Util::MessageBuilder::Cheat::BuildCheatGotoMap(buffer, command))
    {
        networkState.client->Send(buffer);
    }

    return true;
}

bool GameConsoleCommands::HandleGotoLocation(GameConsole* gameConsole, Generated::GotoLocationCommand& command)
{
    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
    ECS::Singletons::NetworkState& networkState = registry->ctx().get<ECS::Singletons::NetworkState>();

    if (!ECS::Util::Network::IsConnected(networkState))
        return false;

    std::shared_ptr<Bytebuffer> buffer = Bytebuffer::Borrow<128>();
    if (ECS::Util::MessageBuilder::Cheat::BuildCheatGotoLocation(buffer, command))
    {
        networkState.client->Send(buffer);
    }

    return true;
}

bool GameConsoleCommands::HandleGotoXYZ(GameConsole* gameConsole, Generated::GotoXYZCommand& command)
{
    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
    ECS::Singletons::NetworkState& networkState = registry->ctx().get<ECS::Singletons::NetworkState>();

    if (!ECS::Util::Network::IsConnected(networkState))
        return false;

    std::shared_ptr<Bytebuffer> buffer = Bytebuffer::Borrow<128>();
    if (ECS::Util::MessageBuilder::Cheat::BuildCheatGotoXYZ(buffer, command))
    {
        networkState.client->Send(buffer);
    }

    return true;
}

bool GameConsoleCommands::HandleTriggerAdd(GameConsole* gameConsole, Generated::TriggerAddCommand& command)
{
    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
    ECS::Singletons::NetworkState& networkState = registry->ctx().get<ECS::Singletons::NetworkState>();

    if (!networkState.client || !networkState.client->IsConnected())
        return false;

    std::shared_ptr<Bytebuffer> buffer = Bytebuffer::Borrow<64>();

    vec3 position = vec3(command.positionX, command.positionY, command.positionZ);
    vec3 extents = vec3(command.extentsX, command.extentsY, command.extentsZ);

    if (!ECS::Util::MessageBuilder::Cheat::BuildCheatTriggerAdd(buffer, command.name, command.flags, command.mapID, position, extents))
        return false;

    networkState.client->Send(buffer);
    return true;
}

bool GameConsoleCommands::HandleTriggerRemove(GameConsole* gameConsole, Generated::TriggerRemoveCommand& command)
{
    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
    ECS::Singletons::NetworkState& networkState = registry->ctx().get<ECS::Singletons::NetworkState>();

    if (!networkState.client || !networkState.client->IsConnected())
        return false;

    std::shared_ptr<Bytebuffer> buffer = Bytebuffer::Borrow<32>();

    if (!ECS::Util::MessageBuilder::Cheat::BuildCheatTriggerRemove(buffer, command.triggerID))
        return false;

    networkState.client->Send(buffer);
    return true;
}

bool GameConsoleCommands::HandleSpellSync(GameConsole* gameConsole, Generated::SpellSyncCommand& command)
{
    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
    ECS::Singletons::NetworkState& networkState = registry->ctx().get<ECS::Singletons::NetworkState>();

    if (!networkState.client || !networkState.client->IsConnected())
        return false;

    entt::registry* dbRegistry = ServiceLocator::GetEnttRegistries()->dbRegistry;
    auto& clientDBSingleton = dbRegistry->ctx().get<ECS::Singletons::ClientDBSingleton>();
    auto& spellSingleton = dbRegistry->ctx().get<ECS::Singletons::SpellSingleton>();
    auto* spellStorage = clientDBSingleton.Get(ClientDBHash::Spell);
    auto* spellEffectsStorage = clientDBSingleton.Get(ClientDBHash::SpellEffects);

    if (!spellStorage->Has(command.spellID))
        return false;

    std::shared_ptr<Bytebuffer> buffer = Bytebuffer::Borrow<1024>();

    auto& spell = spellStorage->Get<Generated::SpellRecord>(command.spellID);

    if (!ECS::Util::MessageBuilder::Cheat::BuildCheatSpellSet(buffer, spellStorage, command.spellID, spell))
        return false;

    const std::vector<u32>* spellEffectList = ECSUtil::Spell::GetSpellEffectList(spellSingleton, command.spellID);
    if (spellEffectList)
    {
        for (u32 spellEffectID : *spellEffectList)
        {
            if (!spellEffectsStorage->Has(spellEffectID))
                continue;

            auto& spellEffect = spellEffectsStorage->Get<Generated::SpellEffectsRecord>(spellEffectID);
            if (!ECS::Util::MessageBuilder::Cheat::BuildCheatSpellEffectSet(buffer, spellEffectsStorage, spellEffectID, spellEffect))
                return false;
        }
    }

    networkState.client->Send(buffer);
    return true;
}

bool GameConsoleCommands::HandleSpellSyncAll(GameConsole* gameConsole, Generated::SpellSyncAllCommand& command)
{
    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
    ECS::Singletons::NetworkState& networkState = registry->ctx().get<ECS::Singletons::NetworkState>();

    if (!networkState.client || !networkState.client->IsConnected())
        return false;

    entt::registry* dbRegistry = ServiceLocator::GetEnttRegistries()->dbRegistry;
    auto& clientDBSingleton = dbRegistry->ctx().get<ECS::Singletons::ClientDBSingleton>();
    auto& spellSingleton = dbRegistry->ctx().get<ECS::Singletons::SpellSingleton>();
    auto* spellStorage = clientDBSingleton.Get(ClientDBHash::Spell);
    auto* spellEffectsStorage = clientDBSingleton.Get(ClientDBHash::SpellEffects);
    auto* spellProcDataStorage = clientDBSingleton.Get(ClientDBHash::SpellProcData);
    auto* spellProcLinkStorage = clientDBSingleton.Get(ClientDBHash::SpellProcLink);

    std::shared_ptr<Bytebuffer> buffer = Bytebuffer::Borrow<65536>();

    bool failed = false;
    spellStorage->Each([&](u32 id, const Generated::SpellRecord& spell)
    {
        if (!ECS::Util::MessageBuilder::Cheat::BuildCheatSpellSet(buffer, spellStorage, id, spell))
        {
            failed = true;
            return false;
        }

        return true;
    });

    spellEffectsStorage->Each([&](u32 id, const Generated::SpellEffectsRecord& spellEffect)
    {
        if (!ECS::Util::MessageBuilder::Cheat::BuildCheatSpellEffectSet(buffer, spellEffectsStorage, id, spellEffect))
        {
            failed = true;
            return false;
        }

        return true;
    });

    spellProcDataStorage->Each([&](u32 id, const Generated::SpellProcDataRecord& spellProcData)
    {
        if (!ECS::Util::MessageBuilder::Cheat::BuildCheatSpellProcDataSet(buffer, spellProcDataStorage, id, spellProcData))
        {
            failed = true;
            return false;
        }

        return true;
    });

    spellProcLinkStorage->Each([&](u32 id, const Generated::SpellProcLinkRecord& spellLinkProc)
    {
        if (!ECS::Util::MessageBuilder::Cheat::BuildCheatSpellProcLinkSet(buffer, spellProcLinkStorage, id, spellLinkProc))
        {
            failed = true;
            return false;
        }

        return true;
    });

    if (failed)
        return false;

    networkState.client->Send(buffer);
    return true;
}

bool GameConsoleCommands::HandleCreatureAddScript(GameConsole* gameConsole, Generated::CreatureAddScriptCommand& command)
{
    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
    ECS::Singletons::NetworkState& networkState = registry->ctx().get<ECS::Singletons::NetworkState>();

    if (!networkState.client || !networkState.client->IsConnected())
        return false;

    std::shared_ptr<Bytebuffer> buffer = Bytebuffer::Borrow<32>();

    if (!ECS::Util::MessageBuilder::Cheat::BuildCreatureAddScript(buffer, command.scriptName))
        return false;

    networkState.client->Send(buffer);
    return true;
}

bool GameConsoleCommands::HandleCreatureRemoveScript(GameConsole* gameConsole, Generated::CreatureRemoveScriptCommand& command)
{
    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
    ECS::Singletons::NetworkState& networkState = registry->ctx().get<ECS::Singletons::NetworkState>();

    if (!networkState.client || !networkState.client->IsConnected())
        return false;

    std::shared_ptr<Bytebuffer> buffer = Bytebuffer::Borrow<32>();

    if (!ECS::Util::MessageBuilder::Cheat::BuildCreatureRemoveScript(buffer))
        return false;

    networkState.client->Send(buffer);
    return true;
}
