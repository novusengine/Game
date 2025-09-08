#pragma once
#include <Base/Types.h>
#include <Base/FunctionTraits.h>
#include <Base/Util/DebugHandler.h>
#include <Base/Util/StringUtils.h>

#include <Meta/Generated/Game/Command.h>

#include <robinhood/robinhood.h>

class GameConsoleCommandHandler;
class GameConsole;

using GameConsoleDynamicCommandHandler = std::function<u32(GameConsole* gameConsole, std::vector<std::string>&)>;

struct GameConsoleCommandEntry
{
    std::string_view name;
    std::string_view help;
    std::string nameWithAliases;
    std::vector<std::string_view> aliases;
    u32 nameHash;
    bool hasParameters = false;

    GameConsoleDynamicCommandHandler callback;
};

class GameConsoleCommandHandler
{
public:
    GameConsoleCommandHandler();

    bool HandleCommand(GameConsole* gameConsole, std::string& command);

    const robin_hood::unordered_map<u32, GameConsoleCommandEntry>& GetCommandEntries() { return _commandRegistry; }

private:
    template <typename CommandHandler>
    bool RegisterCommand(CommandHandler callback)
    {
        // Deduce CommandStruct from the second parameter of the callback
        using CommandStruct = std::decay_t<std::tuple_element_t<1, function_args_t<CommandHandler>>>;
        static_assert(std::is_invocable_r_v<bool, CommandHandler, GameConsole*, CommandStruct&>, "GameConsoleCommandHandler - The Callback provided to 'RegisterCommand' must return a bool and take 2 parameters (GameConsole*, T&)");

        // Static assert to ensure CommandStruct has CommandNameList and Read method
        static_assert(requires { sizeof(CommandStruct::CommandNameList) > 0; }, "CommandStruct must have CommandNameList");
        static_assert(requires { CommandStruct::Read; }, "CommandStruct must have a static Read method");

        // Register the dynamic handler for each command name in CommandNameList
        constexpr u32 numCommandNames = static_cast<u32>(CommandStruct::CommandNameList.size());

        const std::string_view& commandName = CommandStruct::CommandNameList[0];
        u32 commandNameHash = StringUtils::fnv1a_32(commandName.data(), commandName.length());

        if (_commandRegistry.contains(commandNameHash))
        {
            GameConsoleCommandEntry& command = _commandRegistry[commandNameHash];
            NC_LOG_WARNING("[GameConsole] Attempted to register command \"{0}\" but another command \"{1]\" has already been registered with a shared hash", commandName, command.name);
            return false;
        }

        GameConsoleCommandEntry& command = _commandRegistry[commandNameHash];
        command.name = commandName;
        command.help = CommandStruct::CommandHelp;
        command.nameWithAliases = commandName;
        command.nameHash = commandNameHash;
        command.hasParameters = CommandStruct::NumParameters > 0 && CommandStruct::NumParameters != CommandStruct::NumParametersOptional;
        command.callback = [commandName, callback](GameConsole* gameConsole, std::vector<std::string>& parameters) -> u32
        {
            // Returns 0 for Success, 1 for Execution Failed, 2 for Read Failed

            CommandStruct cmd;
            if (CommandStruct::Read(parameters, cmd))
            {
                u32 result = static_cast<u32>(!callback(gameConsole, cmd));
                return result;
            }
            else
            {
                NC_LOG_WARNING("[GameConsole] Failed to read parameters for {0}.", commandName);
                return 2u;
            }
        };

        if (numCommandNames > 1)
        {
            std::string aliases = " (";
            for (u32 commandAliasIndex = 1; commandAliasIndex < numCommandNames; commandAliasIndex++)
            {
                const std::string_view& commandAliasName = CommandStruct::CommandNameList[commandAliasIndex];
                u32 commandAliasNameHash = StringUtils::fnv1a_32(commandAliasName.data(), commandAliasName.length());

                if (_commandRegistry.contains(commandAliasNameHash))
                {
                    NC_LOG_WARNING("[GameConsole] Attempted to register command alias (\"{0}\") but the alias is already used as a command name", commandAliasName);
                    continue;
                }

                if (_commandAliasNameHashToCommandNameHash.contains(commandAliasNameHash))
                {
                    u32 existingCommandHash = _commandAliasNameHashToCommandNameHash[commandAliasNameHash];
                    GameConsoleCommandEntry& existingCommand = _commandRegistry[existingCommandHash];
                    NC_LOG_WARNING("[GameConsole] Attempted to register command alias (\"{0}\" : \"{1}\") but the alias is already in use by the following command \"{2}\"", command.name, commandAliasName, existingCommand.name);
                    continue;
                }

                _commandAliasNameHashToCommandNameHash[commandAliasNameHash] = commandNameHash;
                
                aliases += "'" + std::string(commandAliasName.data(), commandAliasName.length()) + "'";
                if (commandAliasIndex != numCommandNames - 1)
                    aliases += ", ";

                command.aliases.push_back(commandAliasName);
            }

            if (aliases.length() > 2)
            {
                // Check if we need to remove the last comma and space (Could happen if an alias is already in use)
                if (aliases.back() == ' ')
                {
                    aliases.pop_back();
                    aliases.pop_back();
                }

                aliases += ")";
                command.nameWithAliases += aliases;
            }
        }

        return true;
    }

    robin_hood::unordered_map<u32, GameConsoleCommandEntry> _commandRegistry;
    robin_hood::unordered_map<u32, u32> _commandAliasNameHashToCommandNameHash;
};
