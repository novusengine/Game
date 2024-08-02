#include "GameConsoleCommandHandler.h"
#include "GameConsole.h"
#include "GameConsoleCommands.h"

#include <vector>

GameConsoleCommandHandler::GameConsoleCommandHandler()
{
    RegisterCommand("help", GameConsoleCommands::HandleHelp);
    RegisterCommand("ping", GameConsoleCommands::HandlePing);
    RegisterCommand("lua", GameConsoleCommands::HandleDoString);
    RegisterCommand("eval", GameConsoleCommands::HandleDoString);
    RegisterCommand("login", GameConsoleCommands::HandleLogin);
    RegisterCommand("r", GameConsoleCommands::HandleReloadScripts);
    RegisterCommand("reload", GameConsoleCommands::HandleReloadScripts);
    RegisterCommand("reloadscripts", GameConsoleCommands::HandleReloadScripts);
    RegisterCommand("setcursor", GameConsoleCommands::HandleSetCursor);
    RegisterCommand("savecamera", GameConsoleCommands::HandleSaveCamera);
    RegisterCommand("loadcamera", GameConsoleCommands::HandleLoadCamera);
    RegisterCommand("clearmap", GameConsoleCommands::HandleClearMap);
    RegisterCommand("cast", GameConsoleCommands::HandleCast);
    RegisterCommand("damage", GameConsoleCommands::HandleDamage);
    RegisterCommand("kill", GameConsoleCommands::HandleKill);
    RegisterCommand("revive", GameConsoleCommands::HandleRevive);
    RegisterCommand("morph", GameConsoleCommands::HandleMorph);
    RegisterCommand("demorph", GameConsoleCommands::HandleDemorph);
    RegisterCommand("createchar", GameConsoleCommands::HandleCreateChar);
    RegisterCommand("deletechar", GameConsoleCommands::HandleDeleteChar);
}

bool GameConsoleCommandHandler::HandleCommand(GameConsole* gameConsole, std::string& command)
{
    if (command.size() == 0)
        return true;

    std::vector<std::string> splitCommand = StringUtils::SplitString(command);
    u32 hashedCommand = StringUtils::fnv1a_32(splitCommand[0].c_str(), splitCommand[0].size());

    auto commandHandler = _commandHandlers.find(hashedCommand);
    if (commandHandler != _commandHandlers.end())
    {
        splitCommand.erase(splitCommand.begin());
        return commandHandler->second.callback(this, gameConsole, splitCommand);
    }
    else
    {
        gameConsole->PrintWarning("Unhandled command: %s", command.c_str());
        return false;
    }
}