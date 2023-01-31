#include "UpdateScripts.h"

#include "Game/ECS/Singletons/ScriptState.h"
#include "Game/Util/ServiceLocator.h"

#include <Base/Util/DebugHandler.h>

#include <entt/entt.hpp>

#include <Luau/Compiler.h>
#include <lua.h>
#include <luacode.h>
#include <lualib.h>

namespace ECS::Systems
{
	void* Alloc(void* ud, void* luaState, size_t osize, size_t nsize)
	{
		void* result = nullptr;

		if (nsize > 0) {
			result = new u8[nsize];
		}

		return result;
	}

	void UpdateScripts::Init(entt::registry& registry)
	{
        entt::registry::context& ctx = registry.ctx();

        Singletons::ScriptState& scriptState = ctx.emplace<Singletons::ScriptState>();
		{
			lua_State* luaCtx = lua_newstate(Alloc, nullptr);
			luaL_openlibs(luaCtx);

			scriptState.SetLuaCtx(luaCtx);
		}
	}

	void UpdateScripts::Update(entt::registry& registry, f32 deltaTime)
	{
		//entt::registry::context& ctx = registry.ctx();
		//Singletons::ScriptState& scriptState = ctx.at<Singletons::ScriptState>();
		//
		//lua_State* luaCtx = scriptState.GetLuaCtx();
		//lua_resume(luaCtx, luaCtx, 0);
	}
}