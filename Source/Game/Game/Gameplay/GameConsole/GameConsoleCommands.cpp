#include "GameConsoleCommands.h"
#include "GameConsole.h"
#include "Game/Application/EnttRegistries.h"
#include "Game/ECS/Components/Camera.h"
#include "Game/ECS/Util/Transforms.h"
#include "Game/ECS/Singletons/ActiveCamera.h"
#include "Game/ECS/Singletons/NetworkState.h"
#include "Game/Gameplay/MapLoader.h"
#include "Game/Scripting/LuaManager.h"
#include "Game/Util/CameraSaveUtil.h"
#include "Game/Util/ServiceLocator.h"
#include "Game/Rendering/GameRenderer.h"
#include "Game/Rendering/Terrain/TerrainLoader.h"

#include <Base/Memory/Bytebuffer.h>

#include <Network/Define.h>
#include <Network/Client.h>

#include <base64/base64.h>
#include <entt/entt.hpp>

bool GameConsoleCommands::HandleHelp(GameConsole* gameConsole, std::vector<std::string>& subCommands)
{
	gameConsole->Print("-- Help --");
	gameConsole->Print("Available Commands : 'help', 'ping', 'lua', 'eval'");
	return false;
}

bool GameConsoleCommands::HandlePing(GameConsole* gameConsole, std::vector<std::string>& subCommands)
{
	gameConsole->Print("pong");
	return true;
}

bool GameConsoleCommands::HandleDoString(GameConsole* gameConsole, std::vector<std::string>& subCommands)
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

	if (!ServiceLocator::GetLuaManager()->DoString(code))
	{
		gameConsole->PrintError("Failed to run Lua DoString");
	}

	return true;
}

bool GameConsoleCommands::HandleLogin(GameConsole* gameConsole, std::vector<std::string>& subCommands)
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

bool GameConsoleCommands::HandleReloadScripts(GameConsole* gameConsole, std::vector<std::string>& subCommands)
{
	Scripting::LuaManager* luaManager = ServiceLocator::GetLuaManager();
	luaManager->SetDirty();

	return false;
}

bool GameConsoleCommands::HandleSetCursor(GameConsole* gameConsole, std::vector<std::string>& subCommands)
{
	if (subCommands.size() == 0)
		return false;

	std::string& cursorName = subCommands[0];
	u32 hash = StringUtils::fnv1a_32(cursorName.c_str(), cursorName.size());

	GameRenderer* gameRenderer = ServiceLocator::GetGameRenderer();
	gameRenderer->SetCursor(hash);

	return false;
}

bool GameConsoleCommands::HandleSaveCamera(GameConsole* gameConsole, std::vector<std::string>& subCommands)
{
	if (subCommands.size() == 0)
		return false;

	std::string& cameraSaveName = subCommands[0];
	if (cameraSaveName.size() == 0)
		return false;

	std::string saveCode;
	if (Util::CameraSave::GenerateSaveLocation(cameraSaveName, saveCode))
	{
		gameConsole->PrintSuccess("Camera Save Code : %s : %s", cameraSaveName.c_str(), saveCode.c_str());
	}
	else
	{
		gameConsole->PrintError("Failed to generate Camera Save Code %s", cameraSaveName.c_str());
	}

	return false;
}

bool GameConsoleCommands::HandleLoadCamera(GameConsole* gameConsole, std::vector<std::string>& subCommands)
{
	if (subCommands.size() == 0)
		return false;

	std::string& base64 = subCommands[0];

	if (base64.size() == 0)
		return false;

	if (Util::CameraSave::LoadSaveLocationFromBase64(base64))
	{
		gameConsole->PrintSuccess("Loaded Camera Code : %s", base64.c_str());
	}
	else
	{
		gameConsole->PrintError("Failed to load Camera Code %s", base64.c_str());
	}

	return false;
}

bool GameConsoleCommands::HandleClearMap(GameConsole* gameConsole, std::vector<std::string>& subCommands)
{
	MapLoader* mapLoader = ServiceLocator::GetGameRenderer()->GetMapLoader();
	mapLoader->UnloadMap();

	return false;
}