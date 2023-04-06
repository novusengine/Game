#include "GlobalHandler.h"
#include "Game/ECS/Util/MapDBUtil.h"
#include "Game/ECS/Singletons/MapDB.h"
#include "Game/Scripting/LuaStateCtx.h"

#include "Game/Scripting/LuaManager.h"
#include "Game/Scripting/Systems/LuaSystemBase.h"
#include "Game/Util/ServiceLocator.h"
#include "Game/Rendering/GameRenderer.h"
#include "Game/Rendering/Terrain/TerrainLoader.h"
#include "Game/Application/EnttRegistries.h"

#include <entt/entt.hpp>
#include <lualib.h>

namespace Scripting
{
	struct Panel
	{
	public:
		vec3 position;
		vec3 extents;
	};

	void GlobalHandler::Register()
	{
		LuaManager* luaManager = ServiceLocator::GetLuaManager();

		luaManager->SetGlobal("AddCursor", AddCursor, true);
		luaManager->SetGlobal("SetCursor", SetCursor, true);
		luaManager->SetGlobal("GetCurrentMap", GetCurrentMap, true);
		luaManager->SetGlobal("LoadMap", LoadMap, true);

		LuaTable engineTable =
		{
			{
				{ "Name", "Novuscore" },
				{ "Version", vec3(0, 0, 1) },
			}
		};

		luaManager->SetGlobal("Engine", engineTable, true);

		LuaTable panelTable =
		{
			{
				{ "new", PanelCreate },
				{ "GetPosition", PanelGetPosition },
				{ "GetSize", PanelGetExtents }
			}
		};

		LuaTable panelMetaTable =
		{
			{
				{ "__tostring", PanelToString },
				{ "__index", PanelIndex }
			},

			true
		};

		luaManager->SetGlobal("Panel", panelTable, true);
		luaManager->SetGlobal("PanelMetaTable", panelMetaTable, true);
	}

	i32 GlobalHandler::AddCursor(lua_State* state)
	{
		LuaStateCtx ctx(state);

		const char* cursorName = ctx.GetString(nullptr, 1);
		const char* cursorPath = ctx.GetString(nullptr, 2);

		if (cursorName == nullptr || cursorPath == nullptr)
		{
			ctx.PushBool(false);
			return 1;
		}

		u32 hash = StringUtils::fnv1a_32(cursorName, strlen(cursorName));
		std::string path = cursorPath;

		GameRenderer* gameRenderer = ServiceLocator::GetGameRenderer();
		bool result = gameRenderer->AddCursor(hash, path);

		ctx.PushBool(result);
		return 1;
	}
	i32 GlobalHandler::SetCursor(lua_State* state)
	{
		LuaStateCtx ctx(state);

		const char* cursorName = ctx.GetString();
		if (cursorName == nullptr)
		{
			ctx.PushBool(false);
			return 1;
		}

		u32 hash = StringUtils::fnv1a_32(cursorName, strlen(cursorName));

		GameRenderer* gameRenderer = ServiceLocator::GetGameRenderer();
		bool result = gameRenderer->SetCursor(hash);

		ctx.PushBool(result);
		return 1;
	}
	i32 GlobalHandler::GetCurrentMap(lua_State* state)
	{
		LuaStateCtx ctx(state);

		const std::string& currentMapInternalName = ServiceLocator::GetGameRenderer()->GetTerrainLoader()->GetCurrentMapInternalName();

		ctx.PushString(currentMapInternalName.c_str());
		return 1;
	}
	i32 GlobalHandler::LoadMap(lua_State* state)
	{
		LuaStateCtx ctx(state);

		const char* mapName = ctx.GetString();
		if (mapName == nullptr)
		{
			ctx.PushBool(false);
			return 1;
		}

		DB::Client::Definitions::Map* map = ECS::Util::MapDBUtil::GetMapFromName(mapName);
		if (map == nullptr)
		{
			ctx.PushBool(false);
			return 1;
		}

		entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
		entt::registry::context& registryContext = registry->ctx();

		auto& mapDB = registryContext.at<ECS::Singletons::MapDB>();
		const std::string& interalName = mapDB.entries.stringTable.GetString(map->internalName);

		TerrainLoader::LoadDesc loadDesc;
		loadDesc.loadType = TerrainLoader::LoadType::Full;
		loadDesc.mapName = interalName;

		TerrainLoader* terrainLoader = ServiceLocator::GetGameRenderer()->GetTerrainLoader();
		terrainLoader->AddInstance(loadDesc);

		ctx.PushBool(true);
		return 1;
	}
	i32 GlobalHandler::PanelCreate(lua_State* state)
	{
		LuaStateCtx ctx(state);

		Panel* panel = ctx.PushUserData<Panel>([](void* x) {
			// Very sad panel is gone now :(
		});

		panel->position = vec3(25.0f, 50.0f, 0);
		panel->extents = vec3(1.5f, 1.5f, 0);

		luaL_getmetatable(state, "PanelMetaTable");
		lua_setmetatable(state, -2);

		return 1;
	}
	i32 GlobalHandler::PanelGetPosition(lua_State* state)
	{
		LuaStateCtx ctx(state);

		Panel* panel = ctx.GetUserData<Panel>();
		ctx.PushNumber(panel->position.x);
		ctx.PushNumber(panel->position.y);

		return 2;
	}
	i32 GlobalHandler::PanelGetExtents(lua_State* state)
	{
		LuaStateCtx ctx(state);

		Panel* panel = ctx.GetUserData<Panel>();
		ctx.PushNumber(panel->extents.x);
		ctx.PushNumber(panel->extents.y);

		return 2;
	}
	i32 GlobalHandler::PanelToString(lua_State* state)
	{
		LuaStateCtx ctx(state);

		ctx.PushString("Here is a panel :o");
		return 1;
	}
	i32 GlobalHandler::PanelIndex(lua_State* state)
	{
		LuaStateCtx ctx(state);

		Panel* panel = ctx.GetUserData<Panel>(nullptr, 1);
		const char* key = ctx.GetString(nullptr, 2);
		u32 keyHash = StringUtils::fnv1a_32(key, strlen(key));

		switch (keyHash)
		{
			case "position"_h:
			{
				ctx.PushVector(panel->position);
				break;
			}
			case "extents"_h:
			{
				ctx.PushVector(panel->extents);
				break;
			}

			default:
			{
				ctx.GetGlobal("Panel");
				ctx.PushString(key);
				ctx.GetRaw(-2);
			}
		}

		return 1;
	}

}
