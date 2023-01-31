#pragma once
#include "Game/Scripting/ScriptCtx.h"

struct lua_State;
namespace ECS::Singletons
{
	struct ScriptState
	{
	public:
		ScriptCtx ctx;

	public:
		void SetLuaCtx(lua_State* newCtx) { ctx.SetLuaCtx(newCtx); }
		lua_State* GetLuaCtx() { return ctx.GetLuaCtx(); }
	};
}