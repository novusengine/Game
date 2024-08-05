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
        const std::string fileName;
        const std::string filePath;

        const std::string bytecode;
    };

    class LuaManager
    {
    public:
        LuaManager();

        void Init();
        void Update(f32 deltaTime);

        bool DoString(const std::string& code);

        void SetDirty() { _isDirty = true; }

        lua_State* GetInternalState() { return _internalState; }

    private:
        friend LuaHandlerBase;
        friend LuaSystemBase;
        friend GenericSystem;
        friend GameEventHandler;

        void Prepare();
        bool LoadScripts();

        void SetLuaHandler(LuaHandlerType handlerType, LuaHandlerBase* luaHandler);
        void RegisterLuaSystem(LuaSystemBase* systemBase);
        
        template <typename T>
        T GetLuaHandler(LuaHandlerType handler)
        {
            u32 index = static_cast<u32>(handler);
            if (index >= _luaHandlers.size())
                return nullptr;

            return reinterpret_cast<T>(_luaHandlers[index]);
        }

        const std::vector<LuaBytecodeEntry>& GetBytecodeList() { return _bytecodeList; }

    private:
        lua_State* _internalState;
        lua_State* _publicState;

        std::vector<LuaHandlerBase*> _luaHandlers;
        std::vector<LuaSystemBase*> _luaSystems;
        std::vector<enki::TaskSet*> _tasks;

        std::vector<LuaBytecodeEntry> _bytecodeList;
        
        bool _isDirty = false;
    };
}