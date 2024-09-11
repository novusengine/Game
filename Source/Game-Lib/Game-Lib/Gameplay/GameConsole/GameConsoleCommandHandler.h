#pragma once
#include <Base/Types.h>
#include <Base/Util/DebugHandler.h>
#include <Base/Util/StringUtils.h>

#include <robinhood/robinhood.h>

class GameConsoleCommandHandler;
class GameConsole;
struct GameConsoleCommandEntry
{
public:
    std::string name;
    u32 nameHash;

    std::function<bool(GameConsoleCommandHandler*, GameConsole*, std::vector<std::string>&)> callback;
};

class GameConsoleCommandHandler
{
public:
    GameConsoleCommandHandler();

    bool HandleCommand(GameConsole* gameConsole, std::string& command);

    const robin_hood::unordered_map<u16, GameConsoleCommandEntry>& GetCommandEntries() { return _commandHandlers; }

private:
    bool RegisterCommand(const std::string& commandName, const std::function<bool(GameConsoleCommandHandler*, GameConsole*, std::vector<std::string>&)>& handler)
    {
        u32 commandHash = StringUtils::fnv1a_32(commandName.c_str(), commandName.length());

        if (_commandHandlers.contains(commandHash))
        {
            GameConsoleCommandEntry& command = _commandHandlers[commandHash];
            NC_LOG_WARNING("[GameConsole] Attempted to register command \"{0}\" but another command \"{1]\" has already been registered with a shared hash", commandName, command.name);
            return false;
        }

        GameConsoleCommandEntry& command = _commandHandlers[commandHash];
        command.name = commandName;
        command.nameHash = commandHash;
        command.callback = handler;

        return true;
    }

    robin_hood::unordered_map<u16, GameConsoleCommandEntry> _commandHandlers;
};
