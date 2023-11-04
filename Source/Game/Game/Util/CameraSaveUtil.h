#pragma once
#include "ServiceLocator.h"
#include "Game/ECS/Components/Camera.h"
#include "Game/ECS/Singletons/ActiveCamera.h"
#include "Game/ECS/Util/Transforms.h"
#include "Game/Gameplay/MapLoader.h"
#include "Game/Rendering/GameRenderer.h"

#include <Base/Types.h>
#include <Base/Memory/Bytebuffer.h>

#include <entt/entt.hpp>
#include <base64/base64.h>

namespace Util::CameraSave
{
    inline bool GenerateSaveLocation(const std::string& saveName, std::string& result)
    {
		entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
		entt::entity activeCamera = registry->ctx().get<ECS::Singletons::ActiveCamera>().entity;

		const std::string& mapInternalName = ServiceLocator::GetGameRenderer()->GetTerrainLoader()->GetCurrentMapInternalName();

		// SaveName, MapName, Position, Rotation, Scale
		u16 saveNameSize = static_cast<u16>(saveName.size()) + 1;
		u16 mapNameSize = static_cast<u16>(mapInternalName.size()) + 1;

		std::vector<u8> data(saveNameSize + mapNameSize + sizeof(vec3) + sizeof(quat) + sizeof(vec3));
		Bytebuffer buffer = Bytebuffer(data.data(), data.size());

		if (!buffer.PutString(saveName))
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
		result = base64::to_base64(dataView);

		return true;
    }
	inline bool LoadSaveLocationFromBase64(const std::string& base64)
	{
		std::string result = base64::from_base64(base64);
		Bytebuffer buffer = Bytebuffer(result.data(), result.size());

		entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
		entt::entity activeCamera = registry->ctx().get<ECS::Singletons::ActiveCamera>().entity;

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
			MapLoader* mapLoader = ServiceLocator::GetGameRenderer()->GetMapLoader();

			if (mapInternalName.length() == 0)
			{
				mapLoader->UnloadMap();
			}
			else
			{
				u32 mapInternalNameHash = StringUtils::fnv1a_32(mapInternalName.c_str(), mapInternalName.length());
				mapLoader->LoadMapWithInternalName(mapInternalNameHash);
			}
		}

		return true;
	}
}