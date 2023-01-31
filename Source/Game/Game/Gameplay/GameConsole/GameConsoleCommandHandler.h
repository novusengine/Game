#pragma once
#include <Base/Types.h>

#include <robinhood/robinhood.h>

class GameConsole;
class GameConsoleCommandHandler
{
public:
    GameConsoleCommandHandler();

    bool HandleCommand(GameConsole* gameConsole, std::string& command);

private:
    void RegisterCommand(u32 id, const std::function<bool(GameConsole*, std::vector<std::string>)>& handler)
    {
        commandHandlers[id] = handler;
    }

    robin_hood::unordered_map<u16, std::function<bool(GameConsole*, std::vector<std::string>)>> commandHandlers;
};