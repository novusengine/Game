#include "CanvasHandler.h"
#include "Game/Application/EnttRegistries.h"
#include "Game/ECS/Util/MapUtil.h"
#include "Game/ECS/Singletons/MapDB.h"
#include "Game/Gameplay/MapLoader.h"
#include "Game/Rendering/GameRenderer.h"
#include "Game/Scripting/LuaStateCtx.h"
#include "Game/Scripting/LuaManager.h"
#include "Game/Scripting/Systems/LuaSystemBase.h"
#include "Game/Util/ServiceLocator.h"

#include <Base/Util/StringUtils.h>

#include <entt/entt.hpp>
#include <lualib.h>

namespace Scripting
{
	struct Canvas
	{
	public:
		u32 nameHash;
		i32 sizeX;
		i32 sizeY;
	};

	struct Panel
	{
	public:
		i32 posX;
		i32 posY;
		i32 sizeX;
		i32 sizeY;
		u32 layer;

		u32 templateIndex;
	};

	struct Text
	{
	public:
		std::string text;
		i32 posX;
		i32 posY;
		u32 layer;

		u32 templateIndex;
	};

	struct Button
	{
		i32 posX;
		i32 posY;
		i32 sizeX;
		i32 sizeY;
		u32 layer;

		u32 templateIndex;
	};

	void CanvasHandler::Register()
	{
		LuaManager* luaManager = ServiceLocator::GetLuaManager();
		lua_State* state = luaManager->GetInternalState();

		// UI
		LuaTable uiTable =
		{
			{
				// Register templates
				{ "RegisterButtonTemplate", RegisterButtonTemplate },
				{ "RegisterPanelTemplate", RegisterPanelTemplate },
				{ "RegisterTextTemplate", RegisterTextTemplate },
				
				// Canvas
				{ "GetCanvas", GetCanvas },
			}
		};
		luaManager->SetGlobal("UI", uiTable, true);

		// Canvas
		/*LuaTable canvasMetaTable =
		{
			{
				{ "Panel", CreatePanel },
				{ "Text", CreateText }
			},

			true
		};
		luaManager->SetGlobal("CanvasMetaTable", canvasMetaTable, true);*/

		/*luaL_newmetatable(state, "CanvasMetaTable");
		i32 metaTable = lua_gettop(state);
		lua_pushvalue(state, metaTable);
		lua_setglobal(state, "CanvasMetaTable");
		lua_pushvalue(state, metaTable);
		lua_setfield(state, metaTable, "__index");
		lua_pushcfunction(state, CreatePanel, "Panel");
		lua_setfield(state, metaTable, "Panel");
		lua_pushcfunction(state, CreateText, "Text");
		lua_setfield(state, metaTable, "Text");
		lua_pop(state, 1);*/

		// Panel
		LuaTable panelMetaTable =
		{
			{
				{ "Text", CreateText }
			},

			true
		};
		luaManager->SetGlobal("PanelMetaTable", panelMetaTable, true);

		// Text
		/*LuaTable textMetaTable =
		{
			{
				{ "Text", CreateText }
			},

			true
		};
		luaManager->SetGlobal("PanelMetaTable", panelMetaTable, true);*/

	}

	i32 CanvasHandler::RegisterButtonTemplate(lua_State* state)
	{
		LuaStateCtx ctx(state);

		const char* templateName = ctx.GetString(nullptr, 1);

		const char* background = nullptr;
		if (ctx.GetTableField("background", 2))
		{
			background = ctx.GetString(nullptr, 3);
			ctx.Pop(1);
		}

		vec3 color = vec3(1.0f, 1.0f, 1.0f);
		if (ctx.GetTableField("color", 2))
		{
			color = ctx.GetVector(vec3(1,1,1), 3);
			ctx.Pop(1);
		}

		f32 cornerRadius = 0.0f;
		if (ctx.GetTableField("cornerRadius", 2))
		{
			cornerRadius = ctx.GetF32(0.0f, 3);
			ctx.Pop(1);
		}

		return 0;
	}

	i32 CanvasHandler::RegisterPanelTemplate(lua_State* state)
	{
		LuaStateCtx ctx(state);

		const char* templateName = ctx.GetString(nullptr, 1);

		const char* background = nullptr;
		if (ctx.GetTableField("background", 2))
		{
			background = ctx.GetString(nullptr, 3);
			ctx.Pop(1);
		}

		vec3 color = vec3(1.0f, 1.0f, 1.0f);
		if (ctx.GetTableField("color", 2))
		{
			color = ctx.GetVector(vec3(1, 1, 1), 3);
			ctx.Pop(1);
		}

		f32 cornerRadius = 0.0f;
		if (ctx.GetTableField("cornerRadius", 2))
		{
			cornerRadius = ctx.GetF32(0.0f, 3);
			ctx.Pop(1);
		}

		return 0;
	}

	i32 CanvasHandler::RegisterTextTemplate(lua_State* state)
	{
		LuaStateCtx ctx(state);

		const char* templateName = ctx.GetString(nullptr, 1);

		const char* font = nullptr;
		if (ctx.GetTableField("font", 2))
		{
			font = ctx.GetString(nullptr, 3);
			ctx.Pop(1);
		}

		f32 size = 0.0f;
		if (ctx.GetTableField("size", 2))
		{
			size = ctx.GetF32(0.0f, 3);
			ctx.Pop(1);
		}

		vec3 color = vec3(1.0f, 1.0f, 1.0f);
		if (ctx.GetTableField("color", 2))
		{
			color = ctx.GetVector(vec3(1, 1, 1), 3);
			ctx.Pop(1);
		}

		f32 border = 0.0f;
		if (ctx.GetTableField("border", 2))
		{
			border = ctx.GetF32(0.0f, 3);
			ctx.Pop(1);
		}

		vec3 borderColor = vec3(1.0f, 1.0f, 1.0f);
		if (ctx.GetTableField("borderColor", 2))
		{
			borderColor = ctx.GetVector(vec3(1, 1, 1), 3);
			ctx.Pop(1);
		}

		return 0;
	}

	// UI
	i32 CanvasHandler::GetCanvas(lua_State* state)
	{
		LuaStateCtx ctx(state);

		const char* canvasIdentifier = ctx.GetString(nullptr, 1);
		if (canvasIdentifier == nullptr)
		{
			ctx.PushNil();
			return 1;
		}

		i32 sizeX = ctx.GetI32(100, 2);
		i32 sizeY = ctx.GetI32(100, 3);

		Canvas* canvas = ctx.PushUserData<Canvas>([](void* x) {
			// Very sad canvas is gone now :(
		});
		canvas->nameHash = StringUtils::fnv1a_32(canvasIdentifier, strlen(canvasIdentifier));
		canvas->sizeX = sizeX;
		canvas->sizeY = sizeY;

		luaL_getmetatable(state, "CanvasMetaTable");
		lua_setmetatable(state, -2);

		return 1;
	}

	/*i32 CanvasHandler::CanvasIndex(lua_State* state)
	{
		canvas:Panel()
		LuaStateCtx ctx(state);

		Canvas* canvas = ctx.GetUserData<Canvas>(nullptr, 1);
		const char* key = ctx.GetString(nullptr, 2);
		u32 keyHash = StringUtils::fnv1a_32(key, strlen(key));

		switch (keyHash)
		{
		case "Panel"_h:
		{
			return CreatePanel(state, canvas);
		}
		case "Text"_h:
		{
			return CreateText(state, canvas);
		}

		default:
		{
			ctx.GetGlobal("Panel");
			ctx.PushString(key);
			ctx.GetRaw(-2);
		}
		}

		return 1;
	}*/

	// Canvas
	i32 CanvasHandler::CreatePanel(lua_State* state)
	{
		LuaStateCtx ctx(state);

		i32 posX = ctx.GetI32(0, 1);
		i32 posY = ctx.GetI32(0, 2);

		i32 sizeX = ctx.GetI32(100, 3);
		i32 sizeY = ctx.GetI32(100, 4);

		i32 layer = ctx.GetU32(0, 5);

		const char* templateName = ctx.GetString(nullptr, 6);
		if (templateName == nullptr)
		{
			ctx.PushNil();
			return 1;
		}

		Panel* panel = ctx.PushUserData<Panel>([](void* x) {

			});
		panel->posX = posX;
		panel->posY = posY;
		panel->sizeX = sizeX;
		panel->sizeY = sizeY;
		panel->layer = layer;

		// TODO: templateIndex

		luaL_getmetatable(state, "PanelMetaTable");
		lua_setmetatable(state, -2);

		return 1;
	}

	i32 CanvasHandler::CreateText(lua_State* state)
	{
		LuaStateCtx ctx(state);

		const char* str = ctx.GetString("", 1);
		i32 posX = ctx.GetI32(0, 2);
		i32 posY = ctx.GetI32(0, 3);

		i32 layer = ctx.GetU32(0, 4);

		const char* templateName = ctx.GetString(nullptr, 5);
		if (templateName == nullptr)
		{
			ctx.PushNil();
			return 1;
		}

		Text* text = ctx.PushUserData<Text>([](void* x) {

			});
		text->text = str;
		text->posX = posX;
		text->posY = posY;
		text->layer = layer;

		// TODO: templateIndex

		//luaL_getmetatable(state, "TextMetaTable");
		//lua_setmetatable(state, -2);

		return 1;
	}
}
