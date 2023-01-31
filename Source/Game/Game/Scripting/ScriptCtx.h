#pragma once

struct lua_State;

struct ScriptCtx
{
public:
	lua_State* GetLuaCtx() { return _luaCtx; }
	void SetLuaCtx(lua_State* newCtx) { assert(_luaCtx == nullptr); _luaCtx = newCtx; }

private:
	lua_State* _luaCtx = nullptr;
};