#pragma once
#include <Base/Types.h>

#include <robinhood/robinhood.h>

#include <vector>
#include <string>

class Application;
class ConsoleCommandHandler
{
public:
    using CallbackFn = std::function<void(Application&, std::vector<std::string>&)>;
    
    ConsoleCommandHandler();

    void HandleCommand(Application& app, std::string& command);
    void RegisterCommand(u32 commandHash, CallbackFn callback);

private:
    robin_hood::unordered_map<u32, CallbackFn> _commandHashToCallbackFn;
};