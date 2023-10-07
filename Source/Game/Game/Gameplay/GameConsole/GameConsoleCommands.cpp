#include "GameConsoleCommands.h"
#include "GameConsole.h"
#include "Game/Application/EnttRegistries.h"
#include "Game/ECS/Components/Camera.h"
#include "Game/ECS/Util/Transforms.h"
#include "Game/ECS/Singletons/ActiveCamera.h"
#include "Game/ECS/Singletons/NetworkState.h"
#include "Game/Scripting/LuaManager.h"
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

	entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
	entt::entity activeCamera = registry->ctx().at<ECS::Singletons::ActiveCamera>().entity;

	const std::string& mapInternalName = ServiceLocator::GetGameRenderer()->GetTerrainLoader()->GetCurrentMapInternalName();
	
	// SaveName, MapName, Position, Rotation, Scale
	u16 saveNameSize = static_cast<u16>(cameraSaveName.size()) + 1;
	u16 mapNameSize = static_cast<u16>(mapInternalName.size()) + 1;

	std::vector<u8> data(saveNameSize + mapNameSize + sizeof(vec3) + sizeof(quat) + sizeof(vec3));
	Bytebuffer buffer = Bytebuffer(data.data(), data.size());

	if (!buffer.PutString(cameraSaveName))
		return false;

	if (!buffer.PutString(mapInternalName))
		return false;

	{
		ECS::Components::Camera& camera = registry->get<ECS::Components::Camera>(activeCamera);
		ECS::Components::Transform& transform = registry->get<ECS::Components::Transform>(activeCamera);
		
		vec3 position = transform.GetWorldPosition();
		if (!buffer.Put(position))
			return false;

		if (!buffer.Put(camera.pitch))
			return false;

		if (!buffer.Put(camera.yaw))
			return false;

		if (!buffer.Put(camera.roll))
			return false;

		vec3 scale = transform.GetLocalScale();
		if (!buffer.Put(scale))
			return false;
	}

	std::string_view dataView = std::string_view(reinterpret_cast<char*>(data.data()), data.size());
	std::string base64 = base64::to_base64(dataView);

	gameConsole->PrintSuccess("Camera Save Code : %s : %s", cameraSaveName.c_str(), base64.c_str());

	return false;
}

bool GameConsoleCommands::HandleLoadCamera(GameConsole* gameConsole, std::vector<std::string>& subCommands)
{
	if (subCommands.size() == 0)
		return false;

	std::string& base64 = subCommands[0];

	if (base64.size() == 0)
		return false;

	std::string result = base64::from_base64(base64);
	Bytebuffer buffer = Bytebuffer(result.data(), result.size());

	entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
	entt::entity activeCamera = registry->ctx().at<ECS::Singletons::ActiveCamera>().entity;

	std::string cameraSaveName = "";
	std::string mapInternalName = "";

	if (!buffer.GetString(cameraSaveName))
		return false;

	if (!buffer.GetString(mapInternalName))
		return false;

	{
		ECS::Components::Camera& camera = registry->get<ECS::Components::Camera>(activeCamera);
		ECS::Components::Transform& transform = registry->get<ECS::Components::Transform>(activeCamera);

		vec3 position;
		if (!buffer.Get(position))
			return false;

		if (!buffer.Get(camera.pitch))
			return false;

		if (!buffer.Get(camera.yaw))
			return false;

		if (!buffer.Get(camera.roll))
			return false;

		vec3 scale;
		if (!buffer.Get(scale))
			return false;

		ECS::TransformSystem& tf = ECS::TransformSystem::Get(*registry);
		tf.SetWorldPosition(activeCamera, position);
		tf.SetLocalScale(activeCamera, scale);

		camera.dirtyView = true;
		camera.dirtyPerspective = true;
	}

	// Send LoadMap Request
	{
		TerrainLoader* terrainLoader = ServiceLocator::GetGameRenderer()->GetTerrainLoader();

		TerrainLoader::LoadDesc loadDesc;
		loadDesc.mapName = mapInternalName;

		terrainLoader->AddInstance(loadDesc);
	}

	gameConsole->PrintSuccess("Loaded Camera Code : %s : %s", cameraSaveName.c_str(), base64.c_str());

	return false;
}