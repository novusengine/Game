#include "GameConsoleCommandHandler.h"
#include "GameConsole.h"
#include "GameConsoleCommands.h"

#include <vector>

GameConsoleCommandHandler::GameConsoleCommandHandler()
{
    _commandRegistry.reserve(256);
    _commandAliasNameHashToCommandNameHash.reserve(1024);

    //RegisterCommand({ "login" }, "(accountName : string)", GameConsoleCommands::HandleLogin);
    //RegisterCommand({ "cast" }, "(spellID : u32)", GameConsoleCommands::HandleCast);
    //RegisterCommand({ "damage" }, "(damage : u32)", GameConsoleCommands::HandleDamage);
    //RegisterCommand({ "kill" }, "()", GameConsoleCommands::HandleKill);
    //RegisterCommand({ "revive" }, "()", GameConsoleCommands::HandleRevive);
    //RegisterCommand({ "createchar" }, "(name : string)", GameConsoleCommands::HandleCreateChar);
    //RegisterCommand({ "deletechar" }, "(name : string)", GameConsoleCommands::HandleDeleteChar);
    //RegisterCommand({ "setclass" }, "(class : u8)", GameConsoleCommands::HandleSetClass);
    //RegisterCommand({ "setlevel" }, "(level : u16)", GameConsoleCommands::HandleSetLevel);

    RegisterCommand(GameConsoleCommands::HandleHelp);
    RegisterCommand(GameConsoleCommands::HandlePing);
    RegisterCommand(GameConsoleCommands::HandleLua);
    RegisterCommand(GameConsoleCommands::HandleReloadScripts);
    RegisterCommand(GameConsoleCommands::HandleRefreshDB);
    RegisterCommand(GameConsoleCommands::HandleSaveCamera);
    RegisterCommand(GameConsoleCommands::HandleLoadCameraByCode);
    RegisterCommand(GameConsoleCommands::HandleClearMap);
    RegisterCommand(GameConsoleCommands::HandleMorph);
    RegisterCommand(GameConsoleCommands::HandleDemorph);
    RegisterCommand(GameConsoleCommands::HandleFly);
    RegisterCommand(GameConsoleCommands::HandleSetRace);
    RegisterCommand(GameConsoleCommands::HandleSetGender);
    RegisterCommand(GameConsoleCommands::HandleSyncItem);
    RegisterCommand(GameConsoleCommands::HandleForceSyncItems);
    RegisterCommand(GameConsoleCommands::HandleAddItem);
    RegisterCommand(GameConsoleCommands::HandleRemoveItem);
}

bool GameConsoleCommandHandler::HandleCommand(GameConsole* gameConsole, std::string& command)
{
    if (command.size() == 0)
        return true;

    std::vector<std::string> splitCommand = StringUtils::SplitString(command);

    // Reconstruct quoted strings
    std::vector<std::string> processedCommand;
    bool insideQuotes = false;
    std::string quotedString;

    for (std::string& part : splitCommand)
    {
        if (!insideQuotes && part.front() == '"')
        {
            insideQuotes = true;
            quotedString = part.substr(1); // Remove leading quote
        }
        else if (insideQuotes)
        {
            if (part.back() == '"' && (part.size() < 2 || part[part.size() - 2] != '\\'))
            {
                insideQuotes = false;
                quotedString += " " + part.substr(0, part.size() - 1); // Remove trailing quote

                // Replace escaped quotes
                std::string unescapedString;
                for (size_t i = 0; i < quotedString.size(); ++i)
                {
                    if (quotedString[i] == '\\' && i + 1 < quotedString.size() && quotedString[i + 1] == '"')
                        continue; // Skip the backslash, only keep the quote

                    unescapedString += quotedString[i];
                }

                processedCommand.push_back(unescapedString);
            }
            else
            {
                quotedString += " " + part;
            }
        }
        else
        {
            processedCommand.push_back(part);
        }
    }

    if (insideQuotes)
    {
        gameConsole->PrintWarning("Mismatched quotes in command.");
        return false;
    }

    if (processedCommand.empty())
        return true;

    u32 hashedCommand = StringUtils::fnv1a_32(processedCommand[0].c_str(), processedCommand[0].size());
    if (_commandAliasNameHashToCommandNameHash.contains(hashedCommand))
        hashedCommand = _commandAliasNameHashToCommandNameHash[hashedCommand];

    if (_commandRegistry.contains(hashedCommand))
    {
        GameConsoleCommandEntry& commandEntry = _commandRegistry[hashedCommand];
        processedCommand.erase(processedCommand.begin());

        // Returns 0 for Success, 1 for Execution Failed, 2 for Read Failed
        u32 result = commandEntry.callback(gameConsole, processedCommand);
        if (result != 0)
        {
            if (commandEntry.help.length() > 0)
                gameConsole->PrintWarning("Command Help: %s - %s", commandEntry.name.data(), commandEntry.help.data());
        
            return false;
        }

        return true;
    }
    else
    {
        gameConsole->PrintWarning("Unhandled command: %s", command.c_str());
        return false;
    }
}