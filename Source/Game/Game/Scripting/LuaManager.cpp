#include "LuaManager.h"
#include "LuaDefines.h"
#include "LuaState.h"
#include "Handlers/UIHandler.h"
#include "Handlers/GameEventHandler.h"
#include "Handlers/GlobalHandler.h"
#include "Handlers/UIHandler.h"
#include "Systems/LuaSystemBase.h"
#include "Systems/GenericSystem.h"
#include "Game/Util/ServiceLocator.h"

#include <Base/CVarSystem/CVarSystem.h>
#include <Base/Memory/Bytebuffer.h>
#include <Base/Memory/FileReader.h>
#include <Base/Util/DebugHandler.h>

#include <Luau/Compiler.h>
#include <lualib.h>
#include <enkiTS/TaskScheduler.h>

#include <vector>
#include <filesystem>
namespace fs = std::filesystem;

AutoCVar_String CVAR_ScriptDir(CVarCategory::Client, "scriptingDirectory", "defines the directory from where scripts are loaded", "Data/Scripts");
AutoCVar_String CVAR_ScriptExtension(CVarCategory::Client, "scriptingExtension", "defines the file extension to recognized as a script file", ".luau");
AutoCVar_String CVAR_ScriptMotd(CVarCategory::Client, "scriptingMotd", "defines the message of the day passed in the GameLoaded Event", "Welcome to Novuscore");

namespace Scripting
{
    LuaManager::LuaManager() : _internalState(nullptr), _publicState(nullptr)
    {
        _luaHandlers.reserve(16);
        _luaSystems.reserve(16);
    }

    void LuaManager::Init()
    {
        _luaHandlers.resize(static_cast<u32>(LuaHandlerType::Count));
        SetLuaHandler(LuaHandlerType::Global, new GlobalHandler());
        SetLuaHandler(LuaHandlerType::GameEvent, new GameEventHandler());
        SetLuaHandler(LuaHandlerType::UI, new UI::UIHandler());

        Prepare();

        if (LoadScripts())
        {
            RegisterLuaSystem(new GenericSystem());

            LuaGameEventLoadedData eventData;
            eventData.motd = CVAR_ScriptMotd.Get();

            auto gameEventHandler = GetLuaHandler<GameEventHandler*>(LuaHandlerType::GameEvent);
            gameEventHandler->CallEvent(_internalState, static_cast<u32>(LuaGameEvent::Loaded), &eventData);
        }
    }

    void LuaManager::Update(f32 deltaTime)
    {
        bool isDirty = _isDirty;

        if (isDirty)
        {
            Prepare();
            bool result = LoadScripts();

            if (result)
            {
                LuaGameEventLoadedData eventData;
                eventData.motd = CVAR_ScriptMotd.Get();

                auto gameEventHandler = GetLuaHandler<GameEventHandler*>(LuaHandlerType::GameEvent);
                gameEventHandler->CallEvent(_internalState, static_cast<u32>(LuaGameEvent::Loaded), &eventData);

                //auto uiHandler = GetLuaHandler<UI::UIHandler*>(LuaHandlerType::UI);
                //uiHandler->Clear();
            }

            isDirty = result;
        }

        enki::TaskScheduler* scheduler = ServiceLocator::GetTaskScheduler();

        for (u32 i = 0; i < _luaSystems.size(); i++)
        {
            LuaSystemBase* luaSystem = _luaSystems[i];

            if (isDirty)
            {
                luaSystem->PushEvent(LuaSystemEvent::Reload);
            }

            luaSystem->Prepare(deltaTime, _internalState);
            luaSystem->Update(deltaTime, _internalState);

            scheduler->WaitforAll();

            u32 numTasks = static_cast<u32>(_tasks.size());

            for (enki::TaskSet* task : _tasks)
            {
                luaSystem->Run(deltaTime, _internalState);
            }
        }
    }

    bool LuaManager::DoString(const std::string& code)
    {
        LuaState ctx(_internalState);

        Luau::CompileOptions compileOptions;
        {
            compileOptions.optimizationLevel = 1;
            compileOptions.debugLevel = 2;
            compileOptions.coverageLevel = 2;
            compileOptions.vectorLib = "Vector3";
            compileOptions.vectorCtor = "new";
        }

        Luau::ParseOptions parseOptions;
        
        std::string bytecode = Luau::compile(code, compileOptions, parseOptions);
        i32 result = ctx.LoadBytecode("", bytecode, 0);
        if (result != LUA_OK)
        {
            ctx.ReportError();
            return false;
        }
        
        result = ctx.PCall(0, 0);
        return result == LUA_OK;
    }

    void LuaManager::SetLuaHandler(LuaHandlerType handlerType, LuaHandlerBase* luaHandler)
    {
        u32 index = static_cast<u32>(handlerType);

        _luaHandlers[index] = luaHandler;
    }

    void LuaManager::RegisterLuaSystem(LuaSystemBase* systemBase)
    {
        _luaSystems.push_back(systemBase);
    }

    void LuaManager::Prepare()
    {
        _tasks.reserve(32);

        for (u32 i = 0; i < _luaHandlers.size(); i++)
        {
            LuaHandlerBase* base = _luaHandlers[i];

            base->Clear();
        }
    }

    static i32 lua_require(lua_State* state)
    {
        LuaState ctx(state);

        if (lua_gettop(state) != 1)
        {
            luaL_error(state, "error requiring module, invalid number of arguments");
            return 0;
        }

        LuaManager* luaManager = ServiceLocator::GetLuaManager();
        std::string name = luaL_checkstring(state, 1);

        LuaStateInfo* stateInfo = luaManager->GetLuaStateInfo(state);
        if (!stateInfo)
        {
            luaL_error(state, "error requiring module, stateInfo nullptr");
            return 0;
        }

        const char* scriptDirPath = ctx.GetGlobal("path", nullptr);
        if (!scriptDirPath)
        {
            luaL_error(state, "error requiring module, global 'path' missing");
            return 0;
        }

        std::string scriptPath = std::string(scriptDirPath) + "/" + name + ".luau";
        std::replace(scriptPath.begin(), scriptPath.end(), '\\', '/');
        ctx.Pop();

        if (!fs::exists(scriptPath))
        {
            luaL_error(state, "error requiring module, script not found");
            return 0;
        }

        u32 moduleHash = StringUtils::fnv1a_32(scriptPath.c_str(), scriptPath.size());
        if (!stateInfo->_luaPathToBytecodeIndex.contains(moduleHash))
        {
            luaL_error(state, "error requiring module, bytecode not found");
            return 0;
        }

        // Stack: Name, ModuleProxy
        ctx.CreateTable();

        // Stack: Name, ModuleProxy, __ImportedModules__
        ctx.GetGlobalRaw("__ImportedModules__");

        // Stack: Name, ModuleProxy, __ImportedModules__, Module
        if (!ctx.GetTableField(scriptPath))
        {
            u32 bytecodeIndex = stateInfo->_luaPathToBytecodeIndex[moduleHash];
            LuaBytecodeEntry& bytecodeEntry = stateInfo->bytecodeList[bytecodeIndex];

            i32 result = ctx.LoadBytecode(bytecodeEntry.filePath, bytecodeEntry.bytecode, 0);
            if (result != LUA_OK)
            {
                std::string error = lua_tostring(state, -1);

                luaL_error(state, "error requiring module, failed to load module");
                return false;
            }

            if (!ctx.PCall(0, 1))
            {
                return false;
            }

            // Stack: Name, ModuleProxy, __ImportedModules__, Module
            if (lua_gettop(state) == 0)
            {
                luaL_error(state, "error requiring module, module does not return a value");
                return 0;
            }
            else if (!lua_istable(state, -1))
            {
                luaL_error(state, "error requiring module, module must return a table");
                return 0;
            }

            // Stack: Name, ModuleProxy, __ImportedModules__, Module, Module
            ctx.PushValue();

            // Stack: Name, ModuleProxy, __ImportedModules__, Module
            ctx.SetTable("__index");

            // Set the ModuleTable as readonly
            lua_setreadonly(state, -1, true);

            // Add Table to __ImportedModules__
            // Stack: Name, ModuleProxy, __ImportedModules__
            ctx.SetTable(scriptPath.c_str());

            // Push Table to the stack
            ctx.GetTableField(scriptPath);

            bytecodeEntry.isLoaded = true;
        }

        // Stack: Name, ModuleProxy, __ImportedModules__
        lua_setmetatable(state, -3);

        // Stack: Name, ModuleProxy
        ctx.Pop();

        // Stack: ModuleProxy
        lua_replace(state, -2);

        return 1;
    }

    bool LuaManager::LoadScripts()
    {
        const char* scriptDir = CVAR_ScriptDir.Get();
        const char* scriptExtension = CVAR_ScriptExtension.Get();
        fs::path scriptDirectory = fs::absolute(scriptDir);
        std::string scriptDirectoryAsString = scriptDirectory.string();

        if (!fs::exists(scriptDirectory))
        {
            fs::create_directories(scriptDirectory);
        }

        lua_State* state = luaL_newstate();
        u64 key = reinterpret_cast<u64>(state);

        _luaStateToInfo.erase(key);
        LuaStateInfo& stateInfo = _luaStateToInfo[key];

        LuaState ctx(state);
        ctx.RegisterDefaultLibraries();

        ctx.SetGlobal("path", scriptDirectoryAsString.c_str());
        ctx.SetGlobal("require", lua_require);

        for (u32 i = 0; i < _luaHandlers.size(); i++)
        {
            LuaHandlerBase* base = _luaHandlers[i];
            base->Register(state);
        }
        
        luaL_sandbox(state);
        luaL_sandboxthread(state);

        ctx.CreateTable("__ImportedModules__");
        ctx.Pop();

        // TODO : Figure out if this catches hidden folders, and if so exclude them
        // TODO : Should we use a custom file extension for "include" files? Force load any files that for example use ".ext"
        std::vector<std::filesystem::path> paths;
        std::filesystem::recursive_directory_iterator dirpos{ scriptDirectory };
        std::copy(begin(dirpos), end(dirpos), std::back_inserter(paths));

        Luau::CompileOptions compileOptions;
        {
            compileOptions.optimizationLevel = 1;
            compileOptions.debugLevel = 2;
            compileOptions.coverageLevel = 2;
            compileOptions.vectorLib = "vec3";
            compileOptions.vectorType = "vec3";
            compileOptions.vectorCtor = "new";
        }
        Luau::ParseOptions parseOptions;

        bool didFail = false;

        auto gameEventHandler = GetLuaHandler<GameEventHandler*>(LuaHandlerType::GameEvent);
        gameEventHandler->SetupEvents(state);

        for (auto& path : paths)
        {
            if (fs::is_directory(path))
                continue;

            if (path.extension() != scriptExtension)
                continue;

            const std::string pathAsStr = path.string();
            FileReader reader(pathAsStr);

            if (!reader.Open())
                continue;

            u32 bufferSize = static_cast<u32>(reader.Length());
            std::shared_ptr<Bytebuffer> buffer = Bytebuffer::BorrowRuntime(bufferSize);
            reader.Read(buffer.get(), bufferSize);

            std::string luaCode;
            luaCode.resize(bufferSize);

            if (!buffer->GetString(luaCode, bufferSize))
                continue;

            LuaBytecodeEntry bytecodeEntry
            {
                false,
                path.filename().string(),
                pathAsStr,
                Luau::compile(luaCode, compileOptions, parseOptions)
            };

            u32 index = static_cast<u32>(stateInfo.bytecodeList.size());

            fs::path relPath = fs::relative(path, scriptDirectory);
            std::string scriptPath = scriptDirectoryAsString + "/" + relPath.string();
            std::replace(scriptPath.begin(), scriptPath.end(), '\\', '/');

            StringUtils::StringHash pathHash = StringUtils::fnv1a_32(scriptPath.c_str(), scriptPath.size());

            stateInfo.bytecodeList.push_back(bytecodeEntry);
            stateInfo._luaPathToBytecodeIndex[pathHash] = index;
        }

        if (!didFail)
        {
            u32 numScriptsToLoad = static_cast<u32>(stateInfo.bytecodeList.size());

            for (u32 i = 0; i < numScriptsToLoad; i++)
            {
                LuaBytecodeEntry& bytecodeEntry = stateInfo.bytecodeList[i];

                if (bytecodeEntry.isLoaded)
                    continue;

                i32 result = ctx.LoadBytecode(bytecodeEntry.filePath, bytecodeEntry.bytecode, 0);
                if (result != LUA_OK)
                {
                    std::string error = lua_tostring(state, -1);

                    NC_LOG_ERROR("Failed to load script : {0}\n{1}", bytecodeEntry.filePath, error);
                    didFail = true;
                    return false;
                }

                i32 status = ctx.Resume();
                if (status != LUA_OK)
                {
                    std::string error = (status == LUA_YIELD) ? "thread yielded unexpectedly" : lua_tostring(state, -1);
                    error += "\nstacktrace:\n";
                    error += lua_debugtrace(state);

                    NC_LOG_ERROR("Failed to load script : {0}\n{1}", bytecodeEntry.filePath, error);
                    didFail = true;
                    return false;
                }

                NC_LOG_INFO("Loaded Script : {0}", bytecodeEntry.filePath);

                bytecodeEntry.isLoaded = true;
            }
        }

        if (!didFail)
        {
            if (_internalState != nullptr)
            {
                gameEventHandler->ClearEvents(_internalState);
            
                u64 key = reinterpret_cast<u64>(_internalState);
                _luaStateToInfo.erase(key);
                
                LuaState oldCtx(_internalState);
                oldCtx.Close();
            }
            
            _internalState = state;
        }
        else
        {
            gameEventHandler->ClearEvents(state);
        }

        _isDirty = false;
        return !didFail;
    }
}