#pragma once
#include <Base/Types.h>

#include <Meta/Generated/Commands.h>

class GameConsoleCommandHandler;
class GameConsole;

class GameConsoleCommands
{
public:
    //static bool HandleLogin(GameConsoleCommandHandler* commandHandler, GameConsole* gameConsole, std::vector<std::string>& subCommands);
    //static bool HandleCast(GameConsoleCommandHandler* commandHandler, GameConsole* gameConsole, std::vector<std::string>& subCommands);
    //static bool HandleDamage(GameConsoleCommandHandler* commandHandler, GameConsole* gameConsole, std::vector<std::string>& subCommands);
    //static bool HandleKill(GameConsoleCommandHandler* commandHandler, GameConsole* gameConsole, std::vector<std::string>& subCommands);
    //static bool HandleRevive(GameConsoleCommandHandler* commandHandler, GameConsole* gameConsole, std::vector<std::string>& subCommands);
    //static bool HandleCreateChar(GameConsoleCommandHandler* commandHandler, GameConsole* gameConsole, std::vector<std::string>& subCommands);
    //static bool HandleDeleteChar(GameConsoleCommandHandler* commandHandler, GameConsole* gameConsole, std::vector<std::string>& subCommands);
    //static bool HandleSetClass(GameConsoleCommandHandler* commandHandler, GameConsole* gameConsole, std::vector<std::string>& subCommands);
    //static bool HandleSetLevel(GameConsoleCommandHandler* commandHandler, GameConsole* gameConsole, std::vector<std::string>& subCommands);
    
    static bool HandleHelp(GameConsole* gameConsole, Generated::HelpCommand& command);
    static bool HandlePing(GameConsole* gameConsole, Generated::PingCommand& command);
    static bool HandleLua(GameConsole* gameConsole, Generated::LuaCommand& command);
    static bool HandleReloadScripts(GameConsole* gameConsole, Generated::ReloadScriptsCommand& command);
    static bool HandleRefreshDB(GameConsole* gameConsole, Generated::RefreshDBCommand& command);
    static bool HandleSaveCamera(GameConsole* gameConsole, Generated::SaveCameraCommand& command);
    static bool HandleLoadCameraByCode(GameConsole* gameConsole, Generated::LoadCameraByCodeCommand& command);
    static bool HandleClearMap(GameConsole* gameConsole, Generated::ClearMapCommand& command);
    static bool HandleMorph(GameConsole* gameConsole, Generated::MorphCommand& command);
    static bool HandleDemorph(GameConsole* gameConsole, Generated::DemorphCommand& command);
    static bool HandleCharacterCreate(GameConsole* gameConsole, Generated::CharacterCreateCommand& command);
    static bool HandleCharacterDelete(GameConsole* gameConsole, Generated::CharacterDeleteCommand& command);
    static bool HandleFly(GameConsole* gameConsole, Generated::FlyCommand& command);
    static bool HandleSetRace(GameConsole* gameConsole, Generated::SetRaceCommand& command);
    static bool HandleSetGender(GameConsole* gameConsole, Generated::SetGenderCommand& command);
    static bool HandleSyncItem(GameConsole* gameConsole, Generated::SyncItemCommand& command);
    static bool HandleForceSyncItems(GameConsole* gameConsole, Generated::ForceSyncItemsCommand& command);
    static bool HandleAddItem(GameConsole* gameConsole, Generated::AddItemCommand& command);
    static bool HandleRemoveItem(GameConsole* gameConsole, Generated::RemoveItemCommand& command);
    static bool HandleTriggerAdd(GameConsole* gameConsole, Generated::TriggerAddCommand& command);
    static bool HandleTriggerRemove(GameConsole* gameConsole, Generated::TriggerRemoveCommand& command);
};