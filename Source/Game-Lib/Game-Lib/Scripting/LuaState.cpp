#include "LuaState.h"
#include "Handlers/GameEventHandler.h"
#include "Handlers/GlobalHandler.h"

#include <Base/Util/DebugHandler.h>

#include <lualib.h>

namespace Scripting
{
    i32 LuaState::GetStatus()
    {
        return lua_status(_state);
    }

    i32 LuaState::GetTop()
    {
        return lua_gettop(_state);
    }

    void LuaState::SetTop(i32 index)
    {
        lua_settop(_state, index);
    }

    void LuaState::GetRaw(i32 index)
    {
        lua_rawget(_state, index);
    }

    void LuaState::GetRawI(i32 index, i32 n)
    {
        lua_rawgeti(_state, index, n);
    }

    i32 LuaState::GetRef(i32 index)
    {
        return lua_ref(_state, index);
    }

    void LuaState::GetGlobalRaw(const char* key)
    {
        lua_getglobal(_state, key);
    }

    bool LuaState::GetGlobal(const char* key, bool value)
    {
        GetGlobalRaw(key);
        return Get(value);
    }

    i32 LuaState::GetGlobal(const char* key, i32 value)
    {
        GetGlobalRaw(key);
        return Get(value);
    }

    u32 LuaState::GetGlobal(const char* key, u32 value)
    {
        GetGlobalRaw(key);
        return Get(value);
    }

    f32 LuaState::GetGlobal(const char* key, f32 value)
    {
        GetGlobalRaw(key);
        return Get(value);
    }

    f64 LuaState::GetGlobal(const char* key, f64 value)
    {
        GetGlobalRaw(key);
        return Get(value);
    }

    const char* LuaState::GetGlobal(const char* key, const char* value)
    {
        GetGlobalRaw(key);
        return Get(value);
    }

    const vec3 LuaState::GetGlobal(const char* key, const vec3& value)
    {
        GetGlobalRaw(key);
        return Get(value);
    }

    void LuaState::SetGlobal(const char* key)
    {
        lua_setglobal(_state, key);
    }

    void LuaState::SetGlobal(const char* key, bool value)
    {
        Push(value);
        lua_setglobal(_state, key);
    }

    void LuaState::SetGlobal(const char* key, i32 value)
    {
        Push(value);
        lua_setglobal(_state, key);
    }

    void LuaState::SetGlobal(const char* key, u32 value)
    {
        Push(value);
        lua_setglobal(_state, key);
    }

    void LuaState::SetGlobal(const char* key, f32 value)
    {
        Push(value);
        lua_setglobal(_state, key);
    }

    void LuaState::SetGlobal(const char* key, f64 value)
    {
        Push(value);
        lua_setglobal(_state, key);
    }

    void LuaState::SetGlobal(const char* key, const char* value)
    {
        Push(value);
        lua_setglobal(_state, key);
    }

    void LuaState::SetGlobal(const char* key, const vec3& value)
    {
        Push(value);
        lua_setglobal(_state, key);
    }

    void LuaState::SetGlobal(const char* key, const lua_CFunction value)
    {
        Push(value);
        lua_setglobal(_state, key);
    }

    void LuaState::SetField(const char* key, i32 index)
    {
        lua_setfield(_state, index, key);
    }

    template<typename T>
    void LuaState::Push(const T& value) { }

    template<>
    void LuaState::Push(const vec3& value)
    {
        lua_pushvector(_state, value.x, value.y, value.z);
    }

    template<>
    void LuaState::Push(const std::string& value)
    {
        lua_pushstring(_state, value.c_str());
    }

    void LuaState::Push()
    {
        lua_pushnil(_state);
    }

    void LuaState::Push(bool value)
    {
        lua_pushboolean(_state, value);
    }
    
    void LuaState::Push(i32 value)
    {
        lua_pushinteger(_state, value);
    }

    void LuaState::Push(u32 value)
    {
        lua_pushunsigned(_state, value);
    }

    void LuaState::Push(f32 value)
    {
        lua_pushnumber(_state, value);
    }

    void LuaState::Push(f64 value)
    {
        lua_pushnumber(_state, value);
    }

    void LuaState::Push(const char* value)
    {
        lua_pushstring(_state, value);
    }

    void LuaState::Push(lua_CFunction value, const char* debugName)
    {
        lua_pushcfunction(_state, value, debugName);
    }

    void LuaState::PushValue(i32 index)
    {
        lua_pushvalue(_state, index);
    }

    void LuaState::Pop(i32 numPops /*= 1*/)
    {
        lua_pop(_state, numPops);
    }

    bool LuaState::PCall(i32 numArgs /*= 0*/, i32 numResults /*= 0*/, i32 errorfunc /*= 0*/)
    {
        i32 result = lua_pcall(_state, numArgs, numResults, errorfunc);
        if (result != LUA_OK)
        {
            NC_LOG_ERROR("[Scripting] Failed to run a script. Please check the errors below and correct them");
            NC_LOG_ERROR("{0}", Get("Failed to read Luau Runtime Error"));
            Pop();
        }

        return result == LUA_OK;
    }

    bool LuaState::Get(bool fallback /*= false*/, i32 index /*= -1*/)
    {
        if (!lua_isboolean(_state, index))
        {
            return fallback;
        }

        return lua_toboolean(_state, index);
    }

    i32 LuaState::Get(i32 fallback /*= 0*/, i32 index /*= -1*/)
    {
        if (!lua_isnumber(_state, index))
        {
            return fallback;
        }

        return lua_tointeger(_state, index);
    }

    u32 LuaState::Get(u32 fallback /*= 0*/, i32 index /*= -1*/)
    {
        if (!lua_isnumber(_state, index))
        {
            return fallback;
        }

        return lua_tounsigned(_state, index);
    }

    f32 LuaState::Get(f32 fallback /*= 0.0f*/, i32 index /*= -1*/)
    {
        if (!lua_isnumber(_state, index))
        {
            return fallback;
        }

        return static_cast<f32>(lua_tonumber(_state, index));
    }

    f64 LuaState::Get(f64 fallback /*= 0.0*/, i32 index /*= -1*/)
    {
        if (!lua_isnumber(_state, index))
        {
            return fallback;
        }

        return lua_tonumber(_state, index);
    }

    const char* LuaState::Get(const char* fallback, i32 index /*= -1*/)
    {
        if (!lua_isstring(_state, index))
        {
            return fallback;
        }

        return lua_tostring(_state, index);
    }

    vec3 LuaState::Get(vec3 fallback, i32 index)
    {
        if (!lua_isvector(_state, index))
        {
            return fallback;
        }

        const f32* vec = lua_tovector(_state, index);
        return vec3(vec[0], vec[1], vec[2]);
    }

    bool LuaState::GetTableField(const std::string& key, i32 index)
    {
        if (!lua_istable(_state, index))
        {
            return false;
        }

        index += (-1 * (index < 0));

        lua_pushstring(_state, key.c_str());
        lua_gettable(_state, index);

        if (lua_isnil(_state, -1))
        {
            lua_pop(_state, 1);
            return false;
        }

        return true;
    }

    void LuaState::CreateTable()
    {
        lua_newtable(_state);
    }

    void LuaState::CreateTable(const char* name)
    {
        NC_ASSERT(name, "Name is null");

        CreateTable();

        PushValue(-1);
        SetGlobal(name);
    }

    void LuaState::CreateTableAndPopulate(std::function<void()>&& populateFunc)
    {
        NC_ASSERT(populateFunc, "Function pointer is null");

        CreateTable();
        populateFunc();
    }

    void LuaState::CreateTableAndPopulate(const char* name, std::function<void()>&& populateFunc)
    {
        NC_ASSERT(populateFunc, "Function pointer is null");

        if (name)
        {
            CreateTable(name);
            populateFunc();
            Pop();
        }
        else
        {
            CreateTable();
            populateFunc();
        }
    }

    void LuaState::CreateMetaTable(const char* name)
    {
        luaL_newmetatable(_state, name);
    }

    void LuaState::SetTable(const char* key)
    {
        SetField(key);
    }

    void LuaState::SetTable(const char* key, const bool value)
    {
        Push(value);
        SetField(key);
    }

    void LuaState::SetTable(const char* key, const i32 value)
    {
        Push(value);
        SetField(key);
    }

    void LuaState::SetTable(const char* key, const u32 value)
    {
        Push(value);
        SetField(key);
    }

    void LuaState::SetTable(const char* key, const f32 value)
    {
        Push(value);
        SetField(key);
    }

    void LuaState::SetTable(const char* key, const f64 value)
    {
        Push(value);
        SetField(key);
    }

    void LuaState::SetTable(const char* key, const char* value)
    {
        Push(value);
        SetField(key);
    }

    void LuaState::SetTable(const char* key, const vec3& value)
    {
        Push(value);
        SetField(key);
    }

    void LuaState::SetTable(const char* key, const lua_CFunction value)
    {
        Push(value);
        SetField(key);
    }

    void LuaState::SetTable(i32 key)
    {
        lua_rawseti(_state, -2, key);
    }

    void LuaState::SetTable(i32 key, const bool value)
    {
        Push(value);
        SetTable(key);
    }

    void LuaState::SetTable(i32 key, const i32 value)
    {
        Push(value);
        SetTable(key);
    }

    void LuaState::SetTable(i32 key, const u32 value)
    {
        Push(value);
        SetTable(key);
    }

    void LuaState::SetTable(i32 key, const f32 value)
    {
        Push(value);
        SetTable(key);
    }

    void LuaState::SetTable(i32 key, const f64 value)
    {
        Push(value);
        SetTable(key);
    }

    void LuaState::SetTable(i32 key, const char* value)
    {
        Push(value);
        SetTable(key);
    }

    void LuaState::SetTable(i32 key, const vec3& value)
    {
        Push(value);
        SetTable(key);
    }

    void LuaState::SetTable(i32 key, const lua_CFunction value)
    {
        Push(value);
        SetTable(key);
    }

    i32 LuaState::LoadBytecode(const std::string& chunkName, const std::string& bytecode, i32 env)
    {
        return luau_load(_state, chunkName.c_str(), bytecode.c_str(), bytecode.size(), env);
    }

    i32 LuaState::Resume(lua_State* from, i32 nArg)
    {
        return lua_resume(_state, from, nArg);
    }

    void LuaState::MakeReadOnly()
    {
        luaL_sandbox(_state);
    }

    void LuaState::ReportError()
    {
        NC_LOG_ERROR("[Scripting] Please check the errors below and correct them");
        NC_LOG_ERROR("{0}", Get("Failed to read Luau Error"));
        Pop();
    }

    void LuaState::Close()
    {
        lua_close(_state);
    }

    void LuaState::RegisterDefaultLibraries()
    {
        luaL_openlibs(_state);
    }

    void* LuaState::AllocateUserData(lua_State* state, size_t size, LuaUserDataDtor dtor)
    {
        return lua_newuserdatadtor(state, size, dtor);
    }

    bool LuaState::IsUserData(lua_State* state, i32 index)
    {
        return lua_isuserdata(_state, index);
    }

    void* LuaState::ToUserData(lua_State* state, i32 index)
    {
        return lua_touserdata(_state, index);
    }
}
