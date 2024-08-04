#pragma once
#include "LuaDefines.h"

#include <Base/Types.h>

namespace Scripting
{
	struct LuaStateCtx
	{
	public:
		LuaStateCtx(lua_State* state) : _state(state), _pushCounter(0) { assert(state != nullptr); }
		lua_State* GetState() { return _state; }

	public: // Lua Wrapped API
		void GetRaw(i32 index = -1);
		void GetGlobal(const char* key);
		void SetGlobal(const char* key);
		void SetGlobal(const char* key, bool value);
		void SetGlobal(const char* key, i32 value);
		void SetGlobal(const char* key, u32 value);
		void SetGlobal(const char* key, f32 value);
		void SetGlobal(const char* key, f64 value);
		void SetGlobal(const char* key, const char* value);
		void SetGlobal(const char* key, vec3& value);
		void SetGlobal(const char* key, lua_CFunction value);
		void SetGlobal(const char* key, const LuaTable& value);
		void SetGlobal(const LuaTable& value, bool isMetaTable = false);

		i32 GetTop();
		void SetTop(i32 index);

		i32 GetStatus();

		bool PCall(i32 numResults = 0, i32 errorfunc = 0);

		void PushNil(bool incrementPushCounter = true);
		void PushBool(bool value, bool incrementPushCounter = true);
		void PushNumber(i32 value, bool incrementPushCounter = true);
		void PushNumber(u32 value, bool incrementPushCounter = true);
		void PushNumber(f32 value, bool incrementPushCounter = true);
		void PushNumber(f64 value, bool incrementPushCounter = true);
		void PushString(const char* value, bool incrementPushCounter = true);
		void PushVector(vec3& value, bool incrementPushCounter = true);
		void PushCFunction(lua_CFunction func, bool incrementPushCounter = true);
		void PushLFunction(i32 funcRef, bool incrementPushCounter = true);

		template <typename T>
		T* PushUserData(LuaUserDataDtor dtor, bool incrementPushCounter = true)
		{
			T* userData = reinterpret_cast<T*>(AllocateUserData(_state, sizeof(T), dtor));
			new (userData) T(); // Placement New to call constructor

			_pushCounter += 1 * incrementPushCounter;
			return userData;
		}

		void Pop(i32 index = -1);

		bool GetBool(bool fallback = false, i32 index = -1);
		i32 GetI32(i32 fallback = 0, i32 index = -1);
		u32 GetU32(u32 fallback = 0u, i32 index = -1);
		f32 GetF32(f32 fallback = 0.0f, i32 index = -1);
		f64 GetF64(f64 fallback = 0.0, i32 index = -1);
		const char* GetString(const char* fallback = nullptr, i32 index = -1);
		vec3 GetVector(vec3 fallback = vec3(0, 0, 0), i32 index = -1);
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
		i32 GetRef(i32 index = -1);

		void CreateTable();
		void CreateMetaTable(const char* name);
		void SetTable(i32 index = -3);
		void SetTable(const char* key, bool value, i32 index = -3);
		void SetTable(const char* key, i32 value, i32 index = -3);
		void SetTable(const char* key, u32 value, i32 index = -3);
		void SetTable(const char* key, f32 value, i32 index = -3);
		void SetTable(const char* key, f64 value, i32 index = -3);
		void SetTable(const char* key, const char* value, i32 index = -3);
		void SetTable(const char* key, vec3& value, i32 index = -3);
		void SetTable(const char* key, lua_CFunction value, i32 index = -3);
		void SetTable(const char* key, LuaTable& value, i32 index = -3);

		i32 LoadBytecode(const std::string& chunkName, const std::string& bytecode, i32 env = 0);
		i32 Resume(lua_State* from = nullptr, i32 index = 0);
		void MakeReadOnly();
		void ReportError();
		void Close();

	public: // Custom Helper API
		void RegisterDefaultLibraries();

	private: // Internal Helpers
		void SetLuaTable(const char* key, const LuaTable& value, u32 recursiveCounter);

        // Wrappers to get rid of GCC dependency on lua.h in this header
        void* AllocateUserData(lua_State* state, size_t size, LuaUserDataDtor dtor);
        bool IsUserData(lua_State* state, i32 index);
        void* ToUserData(lua_State* state, i32 index);

	private:
		lua_State* _state;
		u32 _pushCounter;
	};
}
