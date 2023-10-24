#include "GameConsoleCommandHandler.h"
#include "GameConsole.h"
#include "GameConsoleCommands.h"

#include <vector>

GameConsoleCommandHandler::GameConsoleCommandHandler()
{
    RegisterCommand("help"_h, GameConsoleCommands::HandleHelp);
    RegisterCommand("ping"_h, GameConsoleCommands::HandlePing);
    RegisterCommand("lua"_h, GameConsoleCommands::HandleDoString);
    RegisterCommand("eval"_h, GameConsoleCommands::HandleDoString);
    RegisterCommand("login"_h, GameConsoleCommands::HandleLogin);
    RegisterCommand("r"_h, GameConsoleCommands::HandleReloadScripts);
    RegisterCommand("reload"_h, GameConsoleCommands::HandleReloadScripts);
    RegisterCommand("reloadscripts"_h, GameConsoleCommands::HandleReloadScripts);
    RegisterCommand("setcursor"_h, GameConsoleCommands::HandleSetCursor);
    RegisterCommand("savecamera"_h, GameConsoleCommands::HandleSaveCamera);
    RegisterCommand("loadcamera"_h, GameConsoleCommands::HandleLoadCamera);
    RegisterCommand("clearmap"_h, GameConsoleCommands::HandleClearMap);
}

bool GameConsoleCommandHandler::HandleCommand(GameConsole* gameConsole, std::string& command)
{
    if (command.size() == 0)
        return true;

    std::vector<std::string> splitCommand = StringUtils::SplitString(command);
    u32 hashedCommand = StringUtils::fnv1a_32(splitCommand[0].c_str(), splitCommand[0].size());

    auto commandHandler = commandHandlers.find(hashedCommand);
    if (commandHandler != commandHandlers.end())
    {
        splitCommand.erase(splitCommand.begin());
        return commandHandler->second(gameConsole, splitCommand);
    }
    else
    {
        gameConsole->PrintWarning("Unhandled command: " + command);
        return false;
    }
}