#pragma once
#include <Base/Types.h>

class GameConsole;
class GameConsoleCommands
{
public:
	static bool HandleHelp(GameConsole* gameConsole, std::vector<std::string>& subCommands);
	static bool HandlePing(GameConsole* gameConsole, std::vector<std::string>& subCommands);
	static bool HandleDoString(GameConsole* gameConsole, std::vector<std::string>& subCommands);
	static bool HandleLogin(GameConsole* gameConsole, std::vector<std::string>& subCommands);
	static bool HandleReloadScripts(GameConsole* gameConsole, std::vector<std::string>& subCommands);
	static bool HandleSetCursor(GameConsole* gameConsole, std::vector<std::string>& subCommands);
	static bool HandleSaveCamera(GameConsole* gameConsole, std::vector<std::string>& subCommands);
	static bool HandleLoadCamera(GameConsole* gameConsole, std::vector<std::string>& subCommands);
	static bool HandleClearMap(GameConsole* gameConsole, std::vector<std::string>& subCommands);
};