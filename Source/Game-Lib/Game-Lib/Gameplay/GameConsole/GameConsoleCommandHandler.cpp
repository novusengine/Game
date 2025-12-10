#include "GameConsoleCommandHandler.h"
#include "GameConsole.h"
#include "GameConsoleCommands.h"

#include <vector>

GameConsoleCommandHandler::GameConsoleCommandHandler()
{
    _commandRegistry.reserve(256);
    _commandAliasNameHashToCommandNameHash.reserve(1024);

    //RegisterCommand({ "setclass" }, "(class : u8)", GameConsoleCommands::HandleSetClass);
    //RegisterCommand({ "setlevel" }, "(level : u16)", GameConsoleCommands::HandleSetLevel);

    RegisterCommand(GameConsoleCommands::HandleHelp);
    RegisterCommand(GameConsoleCommands::HandlePing);
    RegisterCommand(GameConsoleCommands::HandleLua);
    RegisterCommand(GameConsoleCommands::HandleScriptReload);
    RegisterCommand(GameConsoleCommands::HandleDatabaseReload);
    RegisterCommand(GameConsoleCommands::HandleCameraSave);
    RegisterCommand(GameConsoleCommands::HandleCameraLoadByCode);
    RegisterCommand(GameConsoleCommands::HandleMapClear);
    RegisterCommand(GameConsoleCommands::HandleCheatMorph);
    RegisterCommand(GameConsoleCommands::HandleCheatDemorph);
    RegisterCommand(GameConsoleCommands::HandleCharacterAdd);
    RegisterCommand(GameConsoleCommands::HandleCharacterRemove);
    RegisterCommand(GameConsoleCommands::HandleCheatFly);
    RegisterCommand(GameConsoleCommands::HandleCheatSetRace);
    RegisterCommand(GameConsoleCommands::HandleCheatSetGender);
    RegisterCommand(GameConsoleCommands::HandleItemSync);
    RegisterCommand(GameConsoleCommands::HandleItemSyncAll);
    RegisterCommand(GameConsoleCommands::HandleItemAdd);
    RegisterCommand(GameConsoleCommands::HandleItemRemove);
    RegisterCommand(GameConsoleCommands::HandleCreatureAdd);
    RegisterCommand(GameConsoleCommands::HandleCreatureRemove);
    RegisterCommand(GameConsoleCommands::HandleCreatureInfo);
    RegisterCommand(GameConsoleCommands::HandleCheatLogin);
    RegisterCommand(GameConsoleCommands::HandleCheatDamage);
    RegisterCommand(GameConsoleCommands::HandleCheatKill);
    RegisterCommand(GameConsoleCommands::HandleCheatResurrect);
    RegisterCommand(GameConsoleCommands::HandleCheatCast);
    RegisterCommand(GameConsoleCommands::HandleMapSync);
    RegisterCommand(GameConsoleCommands::HandleMapSyncAll);
    RegisterCommand(GameConsoleCommands::HandleGotoAdd);
    RegisterCommand(GameConsoleCommands::HandleGotoAddHere);
    RegisterCommand(GameConsoleCommands::HandleGotoRemove);
    RegisterCommand(GameConsoleCommands::HandleGotoMap);
    RegisterCommand(GameConsoleCommands::HandleGotoLocation);
    RegisterCommand(GameConsoleCommands::HandleGotoXYZ);
    RegisterCommand(GameConsoleCommands::HandleTriggerAdd);
    RegisterCommand(GameConsoleCommands::HandleTriggerRemove);
    RegisterCommand(GameConsoleCommands::HandleSpellSync);
    RegisterCommand(GameConsoleCommands::HandleSpellSyncAll);
    RegisterCommand(GameConsoleCommands::HandleCreatureAddScript);
    RegisterCommand(GameConsoleCommands::HandleCreatureRemoveScript);
    RegisterCommand(GameConsoleCommands::HandleCheatPathGenerate);
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

    u32 numSplitCommands = static_cast<u32>(splitCommand.size());
    for (u32 i = 0; i < numSplitCommands; i++)
    {
        std::string& part = splitCommand[i];

        if (!insideQuotes && part.front() == '"')
        {
            insideQuotes = true;
            quotedString = part.substr(1); // Remove leading quote

            if (i == numSplitCommands - 1)
            {
                if (part.back() == '"' && (part.size() < 2 || part[part.size() - 2] != '\\'))
                {
                    insideQuotes = false;

                    quotedString = quotedString.substr(0, quotedString.size() - 1); // Remove trailing quote

                    // Replace escaped quotes
                    std::string unescapedString;
                    for (size_t j = 0; j < quotedString.size(); ++j)
                    {
                        if (quotedString[j] == '\\' && j + 1 < quotedString.size() && quotedString[j + 1] == '"')
                            continue; // Skip the backslash, only keep the quote

                        unescapedString += quotedString[j];
                    }
                    processedCommand.push_back(unescapedString);
                }
            }
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

    u32 processedCommandSize = static_cast<u32>(processedCommand.size());
    if (processedCommandSize == 0)
        return true;

    std::string candidate;
    for (u32 i = processedCommandSize; i > 0; --i)
    {
        candidate.clear();

        for (u32 j = 0; j < i; j++)
        {
            if (j > 0)
                candidate += ' ';

            candidate += processedCommand[j];
        }

        u32 hashedCommand = StringUtils::fnv1a_32(candidate.c_str(), candidate.size());

        if (_commandAliasNameHashToCommandNameHash.contains(hashedCommand))
            hashedCommand = _commandAliasNameHashToCommandNameHash[hashedCommand];

        if (_commandRegistry.contains(hashedCommand))
        {
            GameConsoleCommandEntry& commandEntry = _commandRegistry[hashedCommand];

            processedCommand.erase(processedCommand.begin(), processedCommand.begin() + i);

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
    }

    gameConsole->PrintWarning("Unhandled command: %s", command.c_str());
    return false;
}