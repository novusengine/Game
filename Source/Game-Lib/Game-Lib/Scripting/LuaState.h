#pragma once
#include "LuaDefines.h"

#include <Base/Types.h>
#include <Base/Util/DebugHandler.h>

namespace Scripting
{
    struct LuaState
    {
    public:
        LuaState(lua_State* state) : _state(state)
        {
            NC_ASSERT(state != nullptr, "State is nullptr");
        }

        lua_State* RawState() { return _state; }

    public: // Lua Wrapped API
        i32 GetStatus();
        i32 GetTop();
        void SetTop(i32 index);
        void GetRaw(i32 index = -1);
        void GetRawI(i32 index, i32 n);
        i32 GetRef(i32 index = -1);

        void GetGlobalRaw(const char* key);
        bool GetGlobal(const char* key, bool fallback = false);
        i32 GetGlobal(const char* key, i32 fallback = 0);
        u32 GetGlobal(const char* key, u32 fallback = 0u);
        f32 GetGlobal(const char* key, f32 fallback = 0.0f);
        f64 GetGlobal(const char* key, f64 fallback = 0.0);
        const char* GetGlobal(const char* key, const char* fallback = nullptr);
        const vec3 GetGlobal(const char* key, const vec3& fallback = vec3(0.0f));

        void SetGlobal(const char* key);
        void SetGlobal(const char* key, bool value);
        void SetGlobal(const char* key, i32 value);
        void SetGlobal(const char* key, u32 value);
        void SetGlobal(const char* key, f32 value);
        void SetGlobal(const char* key, f64 value);
        void SetGlobal(const char* key, const char* value);
        void SetGlobal(const char* key, const vec3& value);
        void SetGlobal(const char* key, const lua_CFunction value);
        void SetField(const char* key, i32 index = -2);

        template<typename T>
        void Push(const T& value);

        void Push();
        void Push(bool value);
        void Push(i32 value);
        void Push(u32 value);
        void Push(f32 value);
        void Push(f64 value);
        void Push(const char* value);
        void Push(lua_CFunction value, const char* debugName = nullptr);
        void PushValue(i32 index = -1);
        void Pop(i32 numPops = 1);

        bool PCall(i32 numArgs = 0, i32 numResults = 0, i32 errorfunc = 0);

        template <typename T>
        T* PushUserData(LuaUserDataDtor dtor)
        {
            T* userData = reinterpret_cast<T*>(AllocateUserData(_state, sizeof(T), dtor));
            new (userData) T(); // Placement New to call constructor
            return userData;
        }

        bool Get(bool fallback = false, i32 index = -1);
        i32 Get(i32 fallback = 0, i32 index = -1);
        u32 Get(u32 fallback = 0u, i32 index = -1);
        f32 Get(f32 fallback = 0.0f, i32 index = -1);
        f64 Get(f64 fallback = 0.0, i32 index = -1);
        const char* Get(const char* fallback = nullptr, i32 index = -1);
        vec3 Get(vec3 fallback = vec3(0.0f), i32 index = -1);
        bool GetTableField(const std::string& key, i32 index = -1);

        template <typename T>
        T* GetUserData(T* fallback = nullptr, i32 index = -1)
        {
            if (!IsUserData(_state, index))
            {
                return fallback;
            }

            return reinterpret_cast<T*>(ToUserData(_state, index));
        }

        void CreateTable();
        void CreateTable(const char* name);
        void CreateTableAndPopulate(std::function<void()>&& populateFunc);
        void CreateTableAndPopulate(const char* name, std::function<void()>&& populateFunc);
        void CreateMetaTable(const char* name);

        void SetTable(const char* key);
        void SetTable(const char* key, const bool value);
        void SetTable(const char* key, const i32 value);
        void SetTable(const char* key, const u32 value);
        void SetTable(const char* key, const f32 value);
        void SetTable(const char* key, const f64 value);
        void SetTable(const char* key, const char* value);
        void SetTable(const char* key, const vec3& value);
        void SetTable(const char* key, const lua_CFunction value);

        void SetTable(i32 key);
        void SetTable(i32 key, const bool value);
        void SetTable(i32 key, const i32 value);
        void SetTable(i32 key, const u32 value);
        void SetTable(i32 key, const f32 value);
        void SetTable(i32 key, const f64 value);
        void SetTable(i32 key, const char* value);
        void SetTable(i32 key, const vec3& value);
        void SetTable(i32 key, const lua_CFunction value);

        i32 LoadBytecode(const std::string& chunkName, const std::string& bytecode, i32 env = 0);
        i32 Resume(lua_State* from = nullptr, i32 nArg = 0);
        void MakeReadOnly();
        void ReportError();
        void Close();

    public: // Custom Helper API
        void RegisterDefaultLibraries();

    private: // Internal Helpers

        // Wrappers to get rid of GCC dependency on lua.h in this header
        void* AllocateUserData(lua_State* state, size_t size, LuaUserDataDtor dtor);
        bool IsUserData(lua_State* state, i32 index);
        void* ToUserData(lua_State* state, i32 index);

    private:
        lua_State* _state;
    };

    template<>
    void LuaState::Push<vec3>(const vec3& value);

    template<>
    void LuaState::Push<std::string>(const std::string& value);
}
