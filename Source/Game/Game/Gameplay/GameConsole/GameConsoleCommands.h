#pragma once
#include <Base/Types.h>

class GameConsole;
class GameConsoleCommands
{
public:
	static bool HandleHelp(GameConsole* gameConsole, std::vector<std::string> subCommands);
	static bool HandlePing(GameConsole* gameConsole, std::vector<std::string> subCommands);
	static bool HandleDoString(GameConsole* gameConsole, std::vector<std::string> subCommands);
	static bool HandleLogin(GameConsole* gameConsole, std::vector<std::string> subCommands);
};