#pragma once
#include "LuaDefines.h"

#include <Base/Types.h>

#include <vector>

namespace enki
{
    class TaskSet;
}

namespace Scripting
{
    class GenericSystem;
    class GameEventHandler;

    struct LuaBytecodeEntry
    {
        bool isLoaded = false;

        const std::string fileName;
        const std::string filePath;

        const std::string bytecode;
    };

    struct LuaStateInfo
    {
    public:
        std::vector<LuaBytecodeEntry> bytecodeList;
        std::vector<LuaBytecodeEntry> apiBytecodeList;
        robin_hood::unordered_map<u32, u32> _luaAPIPathToBytecodeIndex;
    };

    class LuaManager
    {
    public:
        LuaManager();

        void Init();
        void Update(f32 deltaTime);

        bool DoString(const std::string& code);

        void SetDirty() { _isDirty = true; }

        LuaStateInfo* GetLuaStateInfo(lua_State* state)
        {
            u64 key = reinterpret_cast<u64>(state);
            if (!_luaStateToInfo.contains(key))
                return nullptr;

            return &_luaStateToInfo[key];
        }
        lua_State* GetInternalState() { return _internalState; }

        template <typename T>
        T GetLuaHandler(LuaHandlerType handler)
        {
            u32 index = static_cast<u32>(handler);
            if (index >= _luaHandlers.size())
                return nullptr;

            return reinterpret_cast<T>(_luaHandlers[index]);
        }

    private:
        friend LuaHandlerBase;
        friend LuaSystemBase;
        friend GenericSystem;
        friend GameEventHandler;

        void Prepare();
        bool LoadScripts();

        void SetLuaHandler(LuaHandlerType handlerType, LuaHandlerBase* luaHandler);
        void RegisterLuaSystem(LuaSystemBase* systemBase);

    private:
        lua_State* _internalState;
        lua_State* _publicState;

        std::vector<LuaHandlerBase*> _luaHandlers;
        std::vector<LuaSystemBase*> _luaSystems;
        std::vector<enki::TaskSet*> _tasks;

        robin_hood::unordered_map<u64, LuaStateInfo> _luaStateToInfo;
        
        bool _isDirty = false;
    };
}