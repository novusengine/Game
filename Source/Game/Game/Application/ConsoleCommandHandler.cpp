#include "ConsoleCommandHandler.h"
#include "ConsoleCommands.h"

#include <Base/Util/StringUtils.h>
#include <Base/Util/DebugHandler.h>

ConsoleCommandHandler::ConsoleCommandHandler()
{
	RegisterCommand("print"_h, &ConsoleCommands::CommandPrint);
	RegisterCommand("ping"_h, &ConsoleCommands::CommandPing);
	RegisterCommand("lua"_h, &ConsoleCommands::CommandDoString);
	RegisterCommand("eval"_h, &ConsoleCommands::CommandDoString);

	RegisterCommand("exit"_h, &ConsoleCommands::CommandExit);
	RegisterCommand("quit"_h, &ConsoleCommands::CommandExit);
	RegisterCommand("stop"_h, &ConsoleCommands::CommandExit);
}

void ConsoleCommandHandler::HandleCommand(Application& app, std::string& command)
{
	if (command.length() == 0)
		return;

	std::vector<std::string> splitCommand = StringUtils::SplitString(command);
	u32 hashedCommand = StringUtils::fnv1a_32(splitCommand[0].c_str(), splitCommand[0].size());

	auto itr = _commandHashToCallbackFn.find(hashedCommand);
	if (itr == _commandHashToCallbackFn.end())
	{
		DebugHandler::PrintWarning("Unhandled command: (%s)", command.c_str());
		return;
	}

	splitCommand.erase(splitCommand.begin());
	itr->second(app, splitCommand);
}

void ConsoleCommandHandler::RegisterCommand(u32 commandHash, CallbackFn callback)
{
	_commandHashToCallbackFn.insert_or_assign(commandHash, callback);
}
