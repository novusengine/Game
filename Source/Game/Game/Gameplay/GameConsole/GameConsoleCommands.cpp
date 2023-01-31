#include "GameConsoleCommands.h"
#include "GameConsole.h"
#include "Game/Application/EnttRegistries.h"
#include "Game/ECS/Singletons/NetworkState.h"
#include "Game/Scripting/LuaUtil.h"
#include "Game/Util/ServiceLocator.h"

#include <Base/Memory/Bytebuffer.h>

#include <Network/Define.h>
#include <Network/Client.h>

#include <entt/entt.hpp>

bool GameConsoleCommands::HandleHelp(GameConsole* gameConsole, std::vector<std::string> subCommands)
{
	gameConsole->Print("-- Help --");
	gameConsole->Print("Available Commands : 'help', 'ping', 'lua', 'eval'");
	return false;
}

bool GameConsoleCommands::HandlePing(GameConsole* gameConsole, std::vector<std::string> subCommands)
{
	gameConsole->Print("pong");
	return true;
}

bool GameConsoleCommands::HandleDoString(GameConsole* gameConsole, std::vector<std::string> subCommands)
{
	if (subCommands.size() == 0)
		return false;

	std::string code = "";

	for (u32 i = 0; i < subCommands.size(); i++)
	{
		if (i > 0)
		{
			code += " ";
		}

		code += subCommands[i];
	}

	if (!Scripting::LuaUtil::DoString(code))
	{
		gameConsole->PrintError("Failed to run Lua DoString");
	}

	return true;
}

bool GameConsoleCommands::HandleLogin(GameConsole* gameConsole, std::vector<std::string> subCommands)
{
	if (subCommands.size() == 0)
		return false;

	std::string& characterName = subCommands[0];
	if (characterName.size() < 2)
		return false;

	std::shared_ptr<Bytebuffer> buffer = Bytebuffer::Borrow<128>();
	buffer->Put(Network::Opcode::CMSG_CONNECTED);
	buffer->PutU16(static_cast<u16>(characterName.size()) + 1);
	buffer->PutString(characterName);

	entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;

	ECS::Singletons::NetworkState& networkState = registry->ctx().at<ECS::Singletons::NetworkState>();
	networkState.client->Send(buffer);

	return true;
}
