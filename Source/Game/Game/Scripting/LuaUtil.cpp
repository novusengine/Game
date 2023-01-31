#include "LuaUtil.h"
#include "Game/Application/EnttRegistries.h"
#include "Game/ECS/Singletons/ScriptState.h"
#include "Game/Util/ServiceLocator.h"

#include <entt/entt.hpp>

#include <Luau/Compiler.h>
#include <lua.h>
#include <luacode.h>
#include <lualib.h>

using namespace ECS;
using namespace Scripting;

bool LuaUtil::DoString(const std::string& code)
{
	EnttRegistries* enttRegistries = ServiceLocator::GetEnttRegistries();
	entt::registry* gameRegistry = enttRegistries->gameRegistry;

	entt::registry::context& ctx = gameRegistry->ctx();
	Singletons::ScriptState& scriptState = ctx.at<Singletons::ScriptState>();
	lua_State* luaCtx = scriptState.GetLuaCtx();

	Luau::CompileOptions compileOptions;
	Luau::ParseOptions parseOptions;

	std::string bytecode = Luau::compile(code, compileOptions, parseOptions);
	i32 result = luau_load(luaCtx, "", bytecode.c_str(), bytecode.size(), 0);
	if (result != LUA_OK)
		return false;

	result = lua_resume(luaCtx, luaCtx, 0);
	return result == LUA_OK || result == LUA_YIELD || result == LUA_BREAK;
}