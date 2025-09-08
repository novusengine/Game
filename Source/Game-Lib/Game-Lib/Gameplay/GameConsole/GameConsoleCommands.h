#pragma once
#include <Base/Types.h>

#include <Meta/Generated/Game/Command.h>

class GameConsoleCommandHandler;
class GameConsole;

class GameConsoleCommands
{
public:
    //static bool HandleLogin(GameConsoleCommandHandler* commandHandler, GameConsole* gameConsole, std::vector<std::string>& subCommands);
    //static bool HandleCast(GameConsoleCommandHandler* commandHandler, GameConsole* gameConsole, std::vector<std::string>& subCommands);
    //static bool HandleSetClass(GameConsoleCommandHandler* commandHandler, GameConsole* gameConsole, std::vector<std::string>& subCommands);
    //static bool HandleSetLevel(GameConsoleCommandHandler* commandHandler, GameConsole* gameConsole, std::vector<std::string>& subCommands);

    static bool HandleHelp(GameConsole* gameConsole, Generated::HelpCommand& command);
    static bool HandlePing(GameConsole* gameConsole, Generated::PingCommand& command);
    static bool HandleLua(GameConsole* gameConsole, Generated::LuaCommand& command);
    static bool HandleScriptReload(GameConsole* gameConsole, Generated::ScriptReloadCommand& command);
    static bool HandleDatabaseReload(GameConsole* gameConsole, Generated::DatabaseReloadCommand& command);
    static bool HandleCameraSave(GameConsole* gameConsole, Generated::CameraSaveCommand& command);
    static bool HandleCameraLoadByCode(GameConsole* gameConsole, Generated::CameraLoadByCodeCommand& command);
    static bool HandleMapClear(GameConsole* gameConsole, Generated::MapClearCommand& command);
    static bool HandleUnitMorph(GameConsole* gameConsole, Generated::UnitMorphCommand& command);
    static bool HandleUnitDemorph(GameConsole* gameConsole, Generated::UnitDemorphCommand& command);
    static bool HandleCharacterAdd(GameConsole* gameConsole, Generated::CharacterAddCommand& command);
    static bool HandleCharacterRemove(GameConsole* gameConsole, Generated::CharacterRemoveCommand& command);
    static bool HandleCheatFly(GameConsole* gameConsole, Generated::CheatFlyCommand& command);
    static bool HandleUnitSetRace(GameConsole* gameConsole, Generated::UnitSetRaceCommand& command);
    static bool HandleUnitSetGender(GameConsole* gameConsole, Generated::UnitSetGenderCommand& command);
    static bool HandleItemSync(GameConsole* gameConsole, Generated::ItemSyncCommand& command);
    static bool HandleItemSyncAll(GameConsole* gameConsole, Generated::ItemSyncAllCommand& command);
    static bool HandleItemAdd(GameConsole* gameConsole, Generated::ItemAddCommand& command);
    static bool HandleItemRemove(GameConsole* gameConsole, Generated::ItemRemoveCommand& command);
    static bool HandleCreatureAdd(GameConsole* gameConsole, Generated::CreatureAddCommand& command);
    static bool HandleCreatureRemove(GameConsole* gameConsole, Generated::CreatureRemoveCommand& command);
    static bool HandleCreatureInfo(GameConsole* gameConsole, Generated::CreatureInfoCommand& command);
    static bool HandleCheatLogin(GameConsole* gameConsole, Generated::CheatLoginCommand& command);
    static bool HandleCheatDamage(GameConsole* gameConsole, Generated::CheatDamageCommand& command);
    static bool HandleCheatKill(GameConsole* gameConsole, Generated::CheatKillCommand& command);
    static bool HandleCheatResurrect(GameConsole* gameConsole, Generated::CheatResurrectCommand& command);
    static bool HandleCheatCast(GameConsole* gameConsole, Generated::CheatCastCommand& command);
    static bool HandleMapSync(GameConsole* gameConsole, Generated::MapSyncCommand& command);
    static bool HandleMapSyncAll(GameConsole* gameConsole, Generated::MapSyncAllCommand& command);
    static bool HandleGotoAdd(GameConsole* gameConsole, Generated::GotoAddCommand& command);
    static bool HandleGotoAddHere(GameConsole* gameConsole, Generated::GotoAddHereCommand& command);
    static bool HandleGotoRemove(GameConsole* gameConsole, Generated::GotoRemoveCommand& command);
    static bool HandleGotoMap(GameConsole* gameConsole, Generated::GotoMapCommand& command);
    static bool HandleGotoLocation(GameConsole* gameConsole, Generated::GotoLocationCommand& command);
    static bool HandleGotoXYZ(GameConsole* gameConsole, Generated::GotoXYZCommand& command);

    static bool HandleTriggerAdd(GameConsole* gameConsole, Generated::TriggerAddCommand& command);
    static bool HandleTriggerRemove(GameConsole* gameConsole, Generated::TriggerRemoveCommand& command);
};