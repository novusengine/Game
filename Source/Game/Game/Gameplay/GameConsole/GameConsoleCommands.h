#pragma once
#include <Base/Types.h>

class GameConsoleCommandHandler;
class GameConsole;
class GameConsoleCommands
{
public:
	static bool HandleHelp(GameConsoleCommandHandler* commandHandler, GameConsole* gameConsole, std::vector<std::string>& subCommands);
	static bool HandlePing(GameConsoleCommandHandler* commandHandler, GameConsole* gameConsole, std::vector<std::string>& subCommands);
	static bool HandleDoString(GameConsoleCommandHandler* commandHandler, GameConsole* gameConsole, std::vector<std::string>& subCommands);
	static bool HandleLogin(GameConsoleCommandHandler* commandHandler, GameConsole* gameConsole, std::vector<std::string>& subCommands);
	static bool HandleReloadScripts(GameConsoleCommandHandler* commandHandler, GameConsole* gameConsole, std::vector<std::string>& subCommands);
	static bool HandleSetCursor(GameConsoleCommandHandler* commandHandler, GameConsole* gameConsole, std::vector<std::string>& subCommands);
	static bool HandleSaveCamera(GameConsoleCommandHandler* commandHandler, GameConsole* gameConsole, std::vector<std::string>& subCommands);
	static bool HandleLoadCamera(GameConsoleCommandHandler* commandHandler, GameConsole* gameConsole, std::vector<std::string>& subCommands);
	static bool HandleClearMap(GameConsoleCommandHandler* commandHandler, GameConsole* gameConsole, std::vector<std::string>& subCommands);
};