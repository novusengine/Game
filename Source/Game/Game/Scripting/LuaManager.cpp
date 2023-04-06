#include "LuaManager.h"
#include "LuaDefines.h"
#include "LuaStateCtx.h"
#include "Handlers/GameEventHandler.h"
#include "Handlers/GlobalHandler.h"
#include "Systems/LuaSystemBase.h"
#include "Systems/GenericSystem.h"
#include "Game/Util/ServiceLocator.h"

#include <Base/CVarSystem/CVarSystem.h>
#include <Base/Memory/Bytebuffer.h>
#include <Base/Memory/FileReader.h>

#include <Luau/Compiler.h>
#include <lualib.h>
#include <enkiTS/TaskScheduler.h>

#include <vector>
#include <filesystem>
namespace fs = std::filesystem;

AutoCVar_String CVAR_ScriptDir("scripting.directory", "defines the directory from where scripts are loaded", "Data/Scripts");
AutoCVar_String CVAR_ScriptExtension("scripting.extension", "defines the file extension to recognized as a script file", ".luau");
AutoCVar_String CVAR_ScriptMotd("scripting.motd", "defines the message of the day passed in the GameLoaded Event", "Welcome to Novuscore");

namespace Scripting
{
	LuaManager::LuaManager() : _state(nullptr)
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
			RegisterLuaSystem(new GenericSystem(2));

			LuaGameEventLoadedData eventData;
			eventData.motd = CVAR_ScriptMotd.Get();

			auto gameEventHandler = GetLuaHandler<GameEventHandler*>(LuaHandlerType::GameEvent);
			gameEventHandler->CallEvent(_state, static_cast<u32>(LuaGameEvent::Loaded), &eventData);
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
				gameEventHandler->CallEvent(_state, static_cast<u32>(LuaGameEvent::Loaded), &eventData);
			}

			isDirty = result;
		}

		enki::TaskScheduler* scheduler = ServiceLocator::GetTaskScheduler();

		for (u32 i = 0; i < _luaSystems.size(); i++)
		{
			LuaSystemBase* luaSystem = _luaSystems[i];

			u32 numStates = static_cast<u32>(luaSystem->_states.size());
			if (numStates == 0)
				continue;

			if (isDirty)
			{
				luaSystem->PushEvent(LuaSystemEvent::Reload);
			}

			luaSystem->Update(deltaTime);
			luaSystem->Prepare(deltaTime);

			scheduler->WaitforAll();

			for (u32 j = 0; j < numStates; j++)
			{
				if (_tasks.size() <= j)
				{
					_tasks.push_back(new enki::TaskSet(nullptr));
				}

				enki::TaskSet* task = task = _tasks[j];
				task->m_Function = [&luaSystem, deltaTime, j](enki::TaskSetPartition range, uint32_t threadNum)
				{
					luaSystem->Run(deltaTime, j);
				};

				scheduler->AddTaskSetToPipe(task);
			}
		}

		scheduler->WaitforAll();
	}

	bool LuaManager::DoString(const std::string& code)
	{
		LuaStateCtx ctx(_state);

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
		}
		
		result = ctx.Resume();
		if (result != LUA_OK)
		{
			ctx.ReportError();
		}

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
		fs::create_directories(scriptDirectory);

		const LuaTable& table = GetGlobalTable();

		LuaStateCtx ctx(luaL_newstate());
		ctx.RegisterDefaultLibraries();
		ctx.SetGlobal(table);
		ctx.MakeReadOnly();

		std::vector<std::filesystem::path> paths;
		if (paths.size() > 0)
		{
			// TODO : Figure out if this catches hidden folders, and if so exclude them
			// TODO : Should we use a custom file extension for "include" files? Force load any files that for example use ".ext"

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
				if (result != LUA_OK)
				{
					ctx.ReportError();
				}
			}
		}

		auto gameEventHandler = GetLuaHandler<GameEventHandler*>(LuaHandlerType::GameEvent);
		gameEventHandler->SetupEvents(ctx.GetState());

		bool didFail = false;

		i32 top = ctx.GetTop();
		for (i32 i = 0; i < top; i++)
		{
			ctx.Resume();

			i32 result = ctx.GetStatus();
			if (result != LUA_OK)
			{
				ctx.ReportError();
				didFail = true;
				break;
			}
		}

		if (!didFail)
		{
			if (_state != nullptr)
			{
				gameEventHandler->ClearEvents(_state);

				LuaStateCtx oldCtx(_state);
				oldCtx.Close();
			}

			_state = ctx.GetState();
		}
		else
		{
			gameEventHandler->ClearEvents(ctx.GetState());
		}

		_isDirty = false;
		return !didFail;
	}
}