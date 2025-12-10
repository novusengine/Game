#pragma once
#include <Base/Types.h>

#include <MetaGen/Game/Command/Command.h>

class GameConsoleCommandHandler;
class GameConsole;

class GameConsoleCommands
{
public:
    //static bool HandleLogin(GameConsoleCommandHandler* commandHandler, GameConsole* gameConsole, std::vector<std::string>& subCommands);
    //static bool HandleCast(GameConsoleCommandHandler* commandHandler, GameConsole* gameConsole, std::vector<std::string>& subCommands);
    //static bool HandleSetClass(GameConsoleCommandHandler* commandHandler, GameConsole* gameConsole, std::vector<std::string>& subCommands);
    //static bool HandleSetLevel(GameConsoleCommandHandler* commandHandler, GameConsole* gameConsole, std::vector<std::string>& subCommands);

    static bool HandleHelp(GameConsole* gameConsole, MetaGen::Game::Command::HelpCommand& command);
    static bool HandlePing(GameConsole* gameConsole, MetaGen::Game::Command::PingCommand& command);
    static bool HandleLua(GameConsole* gameConsole, MetaGen::Game::Command::LuaCommand& command);
    static bool HandleScriptReload(GameConsole* gameConsole, MetaGen::Game::Command::ScriptReloadCommand& command);
    static bool HandleDatabaseReload(GameConsole* gameConsole, MetaGen::Game::Command::DatabaseReloadCommand& command);
    static bool HandleCameraSave(GameConsole* gameConsole, MetaGen::Game::Command::CameraSaveCommand& command);
    static bool HandleCameraLoadByCode(GameConsole* gameConsole, MetaGen::Game::Command::CameraLoadByCodeCommand& command);
    static bool HandleMapClear(GameConsole* gameConsole, MetaGen::Game::Command::MapClearCommand& command);
    static bool HandleCheatMorph(GameConsole* gameConsole, MetaGen::Game::Command::CheatMorphCommand& command);
    static bool HandleCheatDemorph(GameConsole* gameConsole, MetaGen::Game::Command::CheatDemorphCommand& command);
    static bool HandleCharacterAdd(GameConsole* gameConsole, MetaGen::Game::Command::CharacterAddCommand& command);
    static bool HandleCharacterRemove(GameConsole* gameConsole, MetaGen::Game::Command::CharacterRemoveCommand& command);
    static bool HandleCheatFly(GameConsole* gameConsole, MetaGen::Game::Command::CheatFlyCommand& command);
    static bool HandleCheatSetRace(GameConsole* gameConsole, MetaGen::Game::Command::CheatSetRaceCommand& command);
    static bool HandleCheatSetGender(GameConsole* gameConsole, MetaGen::Game::Command::CheatSetGenderCommand& command);
    static bool HandleItemSync(GameConsole* gameConsole, MetaGen::Game::Command::ItemSyncCommand& command);
    static bool HandleItemSyncAll(GameConsole* gameConsole, MetaGen::Game::Command::ItemSyncAllCommand& command);
    static bool HandleItemAdd(GameConsole* gameConsole, MetaGen::Game::Command::ItemAddCommand& command);
    static bool HandleItemRemove(GameConsole* gameConsole, MetaGen::Game::Command::ItemRemoveCommand& command);
    static bool HandleCreatureAdd(GameConsole* gameConsole, MetaGen::Game::Command::CreatureAddCommand& command);
    static bool HandleCreatureRemove(GameConsole* gameConsole, MetaGen::Game::Command::CreatureRemoveCommand& command);
    static bool HandleCreatureInfo(GameConsole* gameConsole, MetaGen::Game::Command::CreatureInfoCommand& command);
    static bool HandleCheatLogin(GameConsole* gameConsole, MetaGen::Game::Command::CheatLoginCommand& command);
    static bool HandleCheatDamage(GameConsole* gameConsole, MetaGen::Game::Command::CheatDamageCommand& command);
    static bool HandleCheatKill(GameConsole* gameConsole, MetaGen::Game::Command::CheatKillCommand& command);
    static bool HandleCheatResurrect(GameConsole* gameConsole, MetaGen::Game::Command::CheatResurrectCommand& command);
    static bool HandleCheatCast(GameConsole* gameConsole, MetaGen::Game::Command::CheatCastCommand& command);
    static bool HandleCheatPathGenerate(GameConsole* gameConsole, MetaGen::Game::Command::CheatPathGenerateCommand& command);
    static bool HandleMapSync(GameConsole* gameConsole, MetaGen::Game::Command::MapSyncCommand& command);
    static bool HandleMapSyncAll(GameConsole* gameConsole, MetaGen::Game::Command::MapSyncAllCommand& command);
    static bool HandleGotoAdd(GameConsole* gameConsole, MetaGen::Game::Command::GotoAddCommand& command);
    static bool HandleGotoAddHere(GameConsole* gameConsole, MetaGen::Game::Command::GotoAddHereCommand& command);
    static bool HandleGotoRemove(GameConsole* gameConsole, MetaGen::Game::Command::GotoRemoveCommand& command);
    static bool HandleGotoMap(GameConsole* gameConsole, MetaGen::Game::Command::GotoMapCommand& command);
    static bool HandleGotoLocation(GameConsole* gameConsole, MetaGen::Game::Command::GotoLocationCommand& command);
    static bool HandleGotoXYZ(GameConsole* gameConsole, MetaGen::Game::Command::GotoXYZCommand& command);
    static bool HandleTriggerAdd(GameConsole* gameConsole, MetaGen::Game::Command::TriggerAddCommand& command);
    static bool HandleTriggerRemove(GameConsole* gameConsole, MetaGen::Game::Command::TriggerRemoveCommand& command);
    static bool HandleSpellSync(GameConsole* gameConsole, MetaGen::Game::Command::SpellSyncCommand& command);
    static bool HandleSpellSyncAll(GameConsole* gameConsole, MetaGen::Game::Command::SpellSyncAllCommand& command);
    static bool HandleCreatureAddScript(GameConsole* gameConsole, MetaGen::Game::Command::CreatureAddScriptCommand& command);
    static bool HandleCreatureRemoveScript(GameConsole* gameConsole, MetaGen::Game::Command::CreatureRemoveScriptCommand& command);
};