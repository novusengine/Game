#include "LuaStateCtx.h"
#include "Handlers/GameEventHandler.h"
#include "Handlers/GlobalHandler.h"

#include <Base/Util/DebugHandler.h>

#include <lualib.h>

namespace Scripting
{
	void LuaStateCtx::GetRaw(i32 index)
	{
		lua_rawget(_state, index);
	}
	void LuaStateCtx::GetGlobal(const char* key)
	{
		lua_getglobal(_state, key);
	}
	void LuaStateCtx::SetGlobal(const char* key)
	{
		lua_setglobal(_state, key);
		_pushCounter--;
	}
	void LuaStateCtx::SetGlobal(const char* key, bool value)
	{
		PushBool(value);
		SetGlobal(key);
	}
	void LuaStateCtx::SetGlobal(const char* key, i32 value)
	{
		PushNumber(value);
		SetGlobal(key);
	}
	void LuaStateCtx::SetGlobal(const char* key, u32 value)
	{
		PushNumber(value);
		SetGlobal(key);
	}
	void LuaStateCtx::SetGlobal(const char* key, f32 value)
	{
		PushNumber(value);
		SetGlobal(key);
	}
	void LuaStateCtx::SetGlobal(const char* key, f64 value)
	{
		PushNumber(value);
		SetGlobal(key);
	}
	void LuaStateCtx::SetGlobal(const char* key, const char* value)
	{
		PushString(value);
		SetGlobal(key);
	}
	void LuaStateCtx::SetGlobal(const char* key, vec3& value)
	{
		PushVector(value);
		SetGlobal(key);
	}
	void LuaStateCtx::SetGlobal(const char* key, lua_CFunction value)
	{
		PushCFunction(value);
		SetGlobal(key);
	}
	void LuaStateCtx::SetGlobal(const char* key, const LuaTable& value)
	{
		SetLuaTable(key, value, 0);
	}
	void LuaStateCtx::SetGlobal(const LuaTable& value, bool isMetaTable /*= false*/)
	{
		for (const auto& pair : value.data)
		{
			const std::string& key = pair.first;
			const std::any& value = pair.second;

			auto& type = value.type();
			if (value.type() == typeid(bool))
			{
				auto val = std::any_cast<bool>(value);
				SetGlobal(key.c_str(), val);
			}
			else if (value.type() == typeid(i32))
			{
				auto val = std::any_cast<i32>(value);
				SetGlobal(key.c_str(), val);
			}
			else if (value.type() == typeid(u32))
			{
				auto val = std::any_cast<u32>(value);
				SetGlobal(key.c_str(), val);
			}
			else if (value.type() == typeid(f32))
			{
				auto val = std::any_cast<f32>(value);
				SetGlobal(key.c_str(), val);
			}
			else if (value.type() == typeid(f64))
			{
				auto val = std::any_cast<f64>(value);
				SetGlobal(key.c_str(), val);
			}
			else if (value.type() == typeid(vec3))
			{
				auto val = std::any_cast<vec3>(value);
				SetGlobal(key.c_str(), val);
			}
			else if (value.type() == typeid(char*))
			{
				auto val = std::any_cast<char*>(value);
				SetGlobal(key.c_str(), val);
			}
			else if (value.type() == typeid(std::string))
			{
				auto val = std::any_cast<std::string>(value);
				SetGlobal(key.c_str(), val.c_str());
			}
			else if (value.type() == typeid(lua_CFunction))
			{
				auto val = std::any_cast<lua_CFunction>(value);
				SetGlobal(key.c_str(), val);
			}
			else if (value.type() == typeid(LuaTable))
			{
				const auto& val = std::any_cast<LuaTable>(value);
				SetGlobal(key.c_str(), val);
			}
			else
			{
				DebugHandler::PrintFatal("Failed to SetGlobal, invalid typeid");
			}
		}
	}

	i32 LuaStateCtx::GetTop()
	{
		return lua_gettop(_state);
	}

	void LuaStateCtx::SetTop(i32 index)
	{
		lua_settop(_state, index);
	}

	i32 LuaStateCtx::GetStatus()
	{
		return lua_status(_state);
	}

	void LuaStateCtx::PCall(i32 numResults /*= 0*/, i32 errorfunc /*= 0*/)
	{
		u32 numArgs = _pushCounter;

		i32 result = lua_pcall(_state, numArgs, numResults, errorfunc);
		if (result != LUA_OK)
		{
			DebugHandler::PrintError("[Scripting] Failed to run a script. Please check the errors below and correct them");
			DebugHandler::PrintError("{0}", GetString("Failed to read Luau Runtime Error"));
			Pop();
		}

		_pushCounter = 0;
	}

	void LuaStateCtx::PushNil(bool incrementPushCounter /*= true*/)
	{
		lua_pushnil(_state);
		_pushCounter += 1 * incrementPushCounter;
	}
	void LuaStateCtx::PushBool(bool value, bool incrementPushCounter /*= true*/)
	{
		lua_pushboolean(_state, value);
		_pushCounter += 1 * incrementPushCounter;
	}
	void LuaStateCtx::PushNumber(i32 value, bool incrementPushCounter /*= true*/)
	{
		lua_pushnumber(_state, value);
		_pushCounter += 1 * incrementPushCounter;
	}
	void LuaStateCtx::PushNumber(u32 value, bool incrementPushCounter /*= true*/)
	{
		lua_pushnumber(_state, value);
		_pushCounter += 1 * incrementPushCounter;
	}
	void LuaStateCtx::PushNumber(f32 value, bool incrementPushCounter /*= true*/)
	{
		lua_pushnumber(_state, value);
		_pushCounter += 1 * incrementPushCounter;
	}
	void LuaStateCtx::PushNumber(f64 value, bool incrementPushCounter /*= true*/)
	{
		lua_pushnumber(_state, value);
		_pushCounter += 1 * incrementPushCounter;
	}
	void LuaStateCtx::PushString(const char* value, bool incrementPushCounter /*= true*/)
	{
		lua_pushstring(_state, value);
		_pushCounter += 1 * incrementPushCounter;
	}
	void LuaStateCtx::PushVector(vec3& value, bool incrementPushCounter)
	{
		lua_pushvector(_state, value.x, value.y, value.z);
		_pushCounter += 1 * incrementPushCounter;
	}
	void LuaStateCtx::PushCFunction(lua_CFunction func, bool incrementPushCounter /*= true*/)
	{
		lua_pushcfunction(_state, func, nullptr);
		_pushCounter += 1 * incrementPushCounter;
	}
	void LuaStateCtx::PushLFunction(i32 funcRef, bool incrementPushCounter /*= true*/)
	{
		lua_rawgeti(_state, LUA_REGISTRYINDEX, funcRef);
		_pushCounter += 1 * incrementPushCounter;
	}
	void LuaStateCtx::Pop(i32 index /*= -1*/)
	{
		lua_pop(_state, index);
	}

	bool LuaStateCtx::GetBool(bool fallback /*= false*/, i32 index /*= -1*/)
	{
		if (!lua_isboolean(_state, index))
		{
			return fallback;
		}

		return lua_toboolean(_state, index);
	}
	i32 LuaStateCtx::GetI32(i32 fallback /*= 0*/, i32 index /*= -1*/)
	{
		if (!lua_isnumber(_state, index))
		{
			return fallback;
		}

		return lua_tointeger(_state, index);
	}
	u32 LuaStateCtx::GetU32(u32 fallback /*= 0*/, i32 index /*= -1*/)
	{
		if (!lua_isnumber(_state, index))
		{
			return fallback;
		}

		return lua_tounsigned(_state, index);
	}
	f32 LuaStateCtx::GetF32(f32 fallback /*= 0.0f*/, i32 index /*= -1*/)
	{
		if (!lua_isnumber(_state, index))
		{
			return fallback;
		}

		return static_cast<f32>(lua_tonumber(_state, index));
	}
	f64 LuaStateCtx::GetF64(f64 fallback /*= 0.0*/, i32 index /*= -1*/)
	{
		if (!lua_isnumber(_state, index))
		{
			return fallback;
		}

		return lua_tonumber(_state, index);
	}
	const char* LuaStateCtx::GetString(const char* fallback, i32 index /*= -1*/)
	{
		if (!lua_isstring(_state, index))
		{
			return fallback;
		}

		return lua_tostring(_state, index);
	}

	vec3 LuaStateCtx::GetVector(vec3 fallback, i32 index)
	{
		if (!lua_isvector(_state, index))
		{
			return fallback;
		}

		const f32* vec = lua_tovector(_state, index);
		return vec3(vec[0], vec[1], vec[2]);
	}

	i32 LuaStateCtx::GetRef(i32 index)
	{
		return lua_ref(_state, index);
	}

	void LuaStateCtx::CreateTable()
	{
		lua_newtable(_state);
	}
	void LuaStateCtx::CreateMetaTable(const char* name)
	{
		luaL_newmetatable(_state, name);
	}
	void LuaStateCtx::SetTable(i32 index)
	{
		lua_settable(_state, index);
		_pushCounter -= 2;
	}
	void LuaStateCtx::SetTable(const char* key, bool value, i32 index /*= -3*/)
	{
		PushString(key);
		PushBool(value);
		SetTable(index);
	}
	void LuaStateCtx::SetTable(const char* key, i32 value, i32 index /*= -3*/)
	{
		PushString(key);
		PushNumber(value);
		SetTable(index);
	}
	void LuaStateCtx::SetTable(const char* key, u32 value, i32 index /*= -3*/)
	{
		PushString(key);
		PushNumber(value);
		SetTable(index);
	}
	void LuaStateCtx::SetTable(const char* key, f32 value, i32 index /*= -3*/)
	{
		PushString(key);
		PushNumber(value);
		SetTable(index);
	}
	void LuaStateCtx::SetTable(const char* key, f64 value, i32 index /*= -3*/)
	{
		PushString(key);
		PushNumber(value);
		SetTable(index);
	}
	void LuaStateCtx::SetTable(const char* key, const char* value, i32 index /*= -3*/)
	{
		PushString(key);
		PushString(value);
		SetTable(index);
	}
	void LuaStateCtx::SetTable(const char* key, vec3& value, i32 index /*= -3*/)
	{
		PushString(key);
		PushVector(value);
		SetTable(index);
	}
	void LuaStateCtx::SetTable(const char* key, lua_CFunction value, i32 index /*= -3*/)
	{
		PushString(key);
		PushCFunction(value);
		SetTable(index);
	}

	void LuaStateCtx::SetTable(const char* key, LuaTable& value, i32 index /*= -3*/)
	{
		SetLuaTable(key, value, 1);
	}

	i32 LuaStateCtx::LoadBytecode(const std::string& chunkName, const std::string& bytecode, i32 env)
	{
		return luau_load(_state, chunkName.c_str(), bytecode.c_str(), bytecode.size(), env);
	}

	i32 LuaStateCtx::Resume(i32 index, lua_State* from)
	{
		return lua_resume(_state, from, index);
	}

	void LuaStateCtx::MakeReadOnly()
	{
		luaL_sandbox(_state);
	}

	void LuaStateCtx::ReportError()
	{
		DebugHandler::PrintError("[Scripting] Please check the errors below and correct them");
		DebugHandler::PrintError("{0}", GetString("Failed to read Luau Error"));
		Pop();
	}

	void LuaStateCtx::Close()
	{
		lua_close(_state);
	}

	void LuaStateCtx::RegisterDefaultLibraries()
	{
		luaL_openlibs(_state);
	}
	void LuaStateCtx::SetLuaTable(const char* key, const LuaTable& value, u32 recursiveCounter)
	{
		if (recursiveCounter > 0)
		{
			PushString(key);
		}

		if (value.isMetaTable)
		{
			CreateMetaTable(key);
		}
		else
		{
			CreateTable();
		}

		for (const auto& pair : value.data)
		{
			const std::string& key = pair.first;
			const std::any& any = pair.second;

			auto& type = any.type();

			if (any.type() == typeid(bool))
			{
				auto val = std::any_cast<bool>(any);
				SetTable(key.c_str(), val);
			}
			else if (any.type() == typeid(i32))
			{
				auto val = std::any_cast<i32>(any);
				SetTable(key.c_str(), val);
			}
			else if (any.type() == typeid(u32))
			{
				auto val = std::any_cast<u32>(any);
				SetTable(key.c_str(), val);
			}
			else if (any.type() == typeid(f32))
			{
				auto val = std::any_cast<f32>(any);
				SetTable(key.c_str(), val);
			}
			else if (any.type() == typeid(f64))
			{
				auto val = std::any_cast<f64>(any);
				SetTable(key.c_str(), val);
			}
			else if (any.type() == typeid(vec3))
			{
				auto val = std::any_cast<vec3>(any);
				SetTable(key.c_str(), val);
			}
			else if (any.type() == typeid(vec3*))
			{
				auto val = std::any_cast<vec3*>(any);
				SetTable(key.c_str(), val);
			}
			else if (any.type() == typeid(char*))
			{
				auto val = std::any_cast<char*>(any);
				SetTable(key.c_str(), val);
			}
			else if (any.type() == typeid(const char*))
			{
				auto val = std::any_cast<const char*>(any);
				SetTable(key.c_str(), val);
			}
			else if (any.type() == typeid(std::string))
			{
				auto val = std::any_cast<std::string>(any);
				SetTable(key.c_str(), val.c_str());
			}
			else if (any.type() == typeid(std::string*))
			{
				auto val = std::any_cast<std::string*>(any);
				SetTable(key.c_str(), val->c_str());
			}
			else if (any.type() == typeid(lua_CFunction))
			{
				auto val = std::any_cast<lua_CFunction>(any);
				SetTable(key.c_str(), *val);
			}
			else if (any.type() == typeid(lua_CFunction&))
			{
				auto val = std::any_cast<const lua_CFunction&>(any);
				SetTable(key.c_str(), *val);
			}
			else if (any.type() == typeid(lua_CFunction*))
			{
				auto val = std::any_cast<lua_CFunction*>(any);
				SetTable(key.c_str(), val);
			}
			else if (any.type() == typeid(LuaTable))
			{
				auto& val = std::any_cast<const LuaTable>(any);
				SetLuaTable(key.c_str(), val, recursiveCounter + 1);
			}
			else if (any.type() == typeid(LuaTable&))
			{
				const auto& val = std::any_cast<const LuaTable&>(any);
				SetLuaTable(key.c_str(), val, recursiveCounter + 1);
			}
			else
			{
				DebugHandler::PrintFatal("Failed to SetLuaTable, invalid typeid ({0})", any.type().name());
			}
		}

		if (recursiveCounter > 0)
		{
			SetTable();
		}
		else
		{
			SetGlobal(key);
		}
	}

    void* LuaStateCtx::AllocateUserData(lua_State* state, size_t size, LuaUserDataDtor dtor)
    {
        return lua_newuserdatadtor(state, size, dtor);
    }

    bool LuaStateCtx::IsUserData(lua_State* state, i32 index)
    {
        return lua_isuserdata(_state, index);
    }

    void* LuaStateCtx::ToUserData(lua_State* state, i32 index)
    {
        return lua_touserdata(_state, index);
    }
}
