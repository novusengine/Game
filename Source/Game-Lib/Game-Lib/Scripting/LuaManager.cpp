#include "LuaManager.h"
#include "LuaDefines.h"
#include "LuaStateCtx.h"
#include "Handlers/GameEventHandler.h"
#include "Handlers/GlobalHandler.h"
#include "Systems/LuaSystemBase.h"
#include "Systems/GenericSystem.h"
#include "Game-Lib/Util/ServiceLocator.h"

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
        LuaStateCtx ctx(_internalState);

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
            base->Register();
        }
    }

    bool LuaManager::LoadScripts()
    {
        const char* scriptDir = CVAR_ScriptDir.Get();
        const char* scriptExtension = CVAR_ScriptExtension.Get();
        fs::path scriptDirectory = fs::absolute(scriptDir);

        if (!fs::exists(scriptDirectory))
        {
            fs::create_directories(scriptDirectory);
        }

        const LuaTable& table = GetGlobalTable();

        LuaStateCtx ctx(luaL_newstate());
        ctx.RegisterDefaultLibraries();
        ctx.SetGlobal(table);

        luaL_sandbox(ctx.GetState());
        luaL_sandboxthread(ctx.GetState());

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
            compileOptions.vectorLib = "Vector3";
            compileOptions.vectorCtor = "new";
        }
        Luau::ParseOptions parseOptions;

        _bytecodeList.clear();
        _bytecodeList.reserve(paths.size());

        auto gameEventHandler = GetLuaHandler<GameEventHandler*>(LuaHandlerType::GameEvent);
        gameEventHandler->SetupEvents(ctx.GetState());

        bool didFail = false;
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
            if (!buffer->GetString(luaCode, bufferSize))
                continue;

            LuaBytecodeEntry bytecodeEntry
            {
                path.filename().string(),
                path.parent_path().string(),
                Luau::compile(luaCode, compileOptions, parseOptions)
            };

            _bytecodeList.push_back(bytecodeEntry);

            i32 result = ctx.LoadBytecode(pathAsStr, bytecodeEntry.bytecode, 0);
            if (result == LUA_OK)
            {
                i32 status = ctx.Resume();

                if (status == LUA_OK)
                {
                    NC_LOG_INFO("Loaded Script : {0}", pathAsStr);
                }
                else
                {
                    std::string error = (status == LUA_YIELD) ? "thread yielded unexpectedly" : lua_tostring(ctx.GetState(), -1);
                    error += "\nstacktrace:\n";
                    error += lua_debugtrace(ctx.GetState());

                    NC_LOG_ERROR("Failed to load script : {0}\n{1}", pathAsStr, error);
                    didFail = true;
                    break;
                }
            }
            else
            {
                ctx.ReportError();
                didFail = true;
                break;
            }
        }

        if (!didFail)
        {
            if (_internalState != nullptr)
            {
                gameEventHandler->ClearEvents(_internalState);

                LuaStateCtx oldCtx(_internalState);
                oldCtx.Close();
            }

            _internalState = ctx.GetState();
        }
        else
        {
            gameEventHandler->ClearEvents(ctx.GetState());
        }

        _isDirty = false;
        return !didFail;
    }
}