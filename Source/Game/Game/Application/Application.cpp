#include "Application.h"

#include "Game/Animation/AnimationSystem.h"
#include "Game/ECS/Scheduler.h"
#include "Game/ECS/Singletons/EngineStats.h"
#include "Game/ECS/Singletons/RenderState.h"
#include "Game/Editor/EditorHandler.h"
#include "Game/Gameplay/GameConsole/GameConsole.h"
#include "Game/Rendering/GameRenderer.h"
#include "Game/Scripting/LuaManager.h"
#include "Game/Util/ServiceLocator.h"
#include "Game/Loaders/LoaderSystem.h"
#include "Game/Scripting/LuaManager.h"
#include "Game/Scripting/Systems/LuaSystemBase.h"

#include <Base/Types.h>
#include <Base/CVarSystem/CVarSystem.h>
#include <Base/Util/Timer.h>
#include <Base/Util/JsonUtils.h>
#include <Base/Util/DebugHandler.h>
#include <Base/Util/CPUInfo.h>

#include <enkiTS/TaskScheduler.h>
#include <entt/entt.hpp>
#include <imgui/backends/imgui_impl_vulkan.h>
#include <imgui/backends/imgui_impl_glfw.h>
#include <imgui/imguizmo/ImGuizmo.h>
#include <tracy/Tracy.hpp>

AutoCVar_Int CVAR_FramerateLimit("application.framerateLimit", "enable framerate limit", 1, CVarFlags::EditCheckbox);
AutoCVar_Int CVAR_FramerateLimitTarget("application.framerateLimitTarget", "target framerate while limited", 60);
AutoCVar_Int CVAR_CpuReportDetailLevel("application.cpuReportDetailLevel", "Sets the detail level for CPU info printing on startup. (0 = No Output, 1 = CPU Name, 2 = CPU Name + Feature Support)", 1);

Application::Application() : _messagesInbound(256), _messagesOutbound(256) { }
Application::~Application()
{
	delete _gameRenderer;
	delete _editorHandler;
	delete _ecsScheduler;
	delete _taskScheduler;
}

void Application::Start()
{
	if (_isRunning)
		return;

	_isRunning = true;

	std::thread applicationThread = std::thread(&Application::Run, this);
	applicationThread.detach();
}

void Application::Stop()
{
	if (!_isRunning)
		return;

	DebugHandler::Print("Application : Shutdown Initiated");
	Cleanup();
	DebugHandler::Print("Application : Shutdown Complete");

	MessageOutbound message(MessageOutbound::Type::Exit);
	_messagesOutbound.enqueue(message);
}

void Application::Cleanup()
{
}

void Application::PassMessage(MessageInbound& message)
{
	_messagesInbound.enqueue(message);
}

bool Application::TryGetMessageOutbound(MessageOutbound& message)
{
	bool messageFound = _messagesOutbound.try_dequeue(message);
	return messageFound;
}

void Application::Run()
{
	tracy::SetThreadName("Application Thread");

	if (Init())
	{
		Timer timer;
		Timer updateTimer;
		Timer renderTimer;

		entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;

		ECS::Singletons::RenderState& renderState = registry->ctx().at<ECS::Singletons::RenderState>();
		
		ECS::Singletons::EngineStats& engineStats = registry->ctx().at<ECS::Singletons::EngineStats>();
		ECS::Singletons::FrameTimes timings;		
		while (true)
		{
			f32 deltaTime = timer.GetDeltaTime();
			timer.Tick();

			timings.deltaTimeS = deltaTime;

			updateTimer.Reset();
			renderState.frameNumber++;

			if (!Tick(deltaTime))
				break;

			timings.simulationFrameTimeS = updateTimer.GetLifeTime();
			renderTimer.Reset();

			ServiceLocator::GetGameConsole()->Render(deltaTime);

			_editorHandler->DrawImGuiMenuBar(deltaTime);

			f32 timeSpentWaiting = 0.0f;
			if (!Render(deltaTime, timeSpentWaiting))
				break;

			timings.renderFrameTimeS = renderTimer.GetLifeTime() - timeSpentWaiting;
			timings.renderWaitTimeS = timeSpentWaiting;

			// Get last GPU Frame time
			Renderer::Renderer* renderer = _gameRenderer->GetRenderer();

			const std::vector<Renderer::TimeQueryID> frameTimeQueries = renderer->GetFrameTimeQueries();

			for (Renderer::TimeQueryID timeQueryID : frameTimeQueries)
			{
				const std::string& name = renderer->GetTimeQueryName(timeQueryID);
				f32 durationMS = renderer->GetLastTimeQueryDuration(timeQueryID);

				engineStats.AddNamedStat(name, durationMS);
			}

			Renderer::TimeQueryID totalTimeQuery = frameTimeQueries[0];
			timings.gpuFrameTimeMS = renderer->GetLastTimeQueryDuration(totalTimeQuery);

			engineStats.AddTimings(timings.deltaTimeS, timings.simulationFrameTimeS, timings.renderFrameTimeS, timings.renderWaitTimeS, timings.gpuFrameTimeMS);

			bool limitFrameRate = CVAR_FramerateLimit.Get() == 1;
			if (limitFrameRate)
			{
				f32 targetFramerate = Math::Max(static_cast<f32>(CVAR_FramerateLimitTarget.Get()), 10.0f);
				f32 targetDelta = 1.0f / targetFramerate;

				for (deltaTime = timer.GetDeltaTime(); deltaTime < targetDelta; deltaTime = timer.GetDeltaTime())
				{
					std::this_thread::yield();
				}
			}

			FrameMark;
		}
	}

	Stop();
}

bool Application::Init()
{
	// Setup CVar Config
	{
		std::filesystem::create_directories("Data/Config");

		nlohmann::ordered_json fallback;
		if (JsonUtils::LoadFromPathOrCreate(_cvarJson, fallback, "Data/Config/CVar.json"))
		{
			JsonUtils::LoadJsonIntoCVars(_cvarJson);
			JsonUtils::LoadCVarsIntoJson(_cvarJson);
			JsonUtils::SaveToPath(_cvarJson, "Data/Config/CVar.json");
		}
	}

	// Print CPU info
	CPUInfo cpuInfo = CPUInfo::Get();
	cpuInfo.Print(CVAR_CpuReportDetailLevel.Get());

	_taskScheduler = new enki::TaskScheduler();
	_taskScheduler->Initialize();
	ServiceLocator::SetTaskScheduler(_taskScheduler);

	_registries.gameRegistry = new entt::registry();
	ServiceLocator::SetEnttRegistries(&_registries);

	_gameRenderer = new GameRenderer();
	_editorHandler = new Editor::EditorHandler();
	ServiceLocator::SetEditorHandler(_editorHandler);

	_ecsScheduler = new ECS::Scheduler();
	_ecsScheduler->Init(*_registries.gameRegistry);

	LoaderSystem* loaderSystem = LoaderSystem::Get();
	loaderSystem->Init();

	bool failedToLoad = false;
	for (Loader* loader : loaderSystem->GetLoaders())
	{
		failedToLoad |= !loader->Init();

		if (failedToLoad)
			break;
	}

	if (failedToLoad)
		return false;

	ServiceLocator::SetGameConsole(new GameConsole());

	_luaManager = new Scripting::LuaManager();
	ServiceLocator::SetLuaManager(_luaManager);
	_luaManager->Init();

	// Init AnimationSystem
	{
		// ModelRenderer is optional and nullptr can be passed in to run the AnimationSystem without it
		ModelRenderer* modelRenderer = _gameRenderer->GetModelRenderer();
		_animationSystem = new Animation::AnimationSystem(modelRenderer);

		ServiceLocator::SetAnimationSystem(_animationSystem);
	}

	return true;
}

bool Application::Tick(f32 deltaTime)
{
	bool shouldExit = !_gameRenderer->UpdateWindow(deltaTime);
	if (shouldExit)
		return false;

	// Imgui New Frame
	{
		_editorHandler->NewFrame();
		ImGui_ImplVulkan_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();
		ImGuizmo::BeginFrame();
	}

	_editorHandler->BeginImGui();
	_editorHandler->BeginEditor();

	MessageInbound message;
	while (_messagesInbound.try_dequeue(message))
	{
		assert(message.type != MessageInbound::Type::Invalid);

		switch (message.type)
		{
			case MessageInbound::Type::Print:
			{
				DebugHandler::Print(message.data);
				break;
			}

			case MessageInbound::Type::Ping:
			{
				MessageOutbound pongMessage(MessageOutbound::Type::Pong);
				_messagesOutbound.enqueue(pongMessage);

				DebugHandler::Print("Main Thread -> Application Thread : Ping");
				break;
			}

			case MessageInbound::Type::DoString:
			{
				if (!ServiceLocator::GetLuaManager()->DoString(message.data))
				{
					DebugHandler::PrintError("Failed to run Lua DoString");
				}
				
				break;
			}

			case MessageInbound::Type::ReloadScripts:
			{
				ServiceLocator::GetLuaManager()->SetDirty();
				break;
			}

			case MessageInbound::Type::Exit:
				return false;

			default: break;
		}
	}

	_ecsScheduler->Update(*_registries.gameRegistry, deltaTime);
	_animationSystem->Update(deltaTime);

	_editorHandler->Update(deltaTime);

	_gameRenderer->UpdateRenderers(deltaTime);

	if (CVarSystem::Get()->IsDirty())
	{
		JsonUtils::LoadCVarsIntoJson(_cvarJson);
		JsonUtils::SaveToPath(_cvarJson, "Data/Config/CVar.json");

		CVarSystem::Get()->ClearDirty();
	}

	return true;
}

bool Application::Render(f32 deltaTime, f32& timeSpentWaiting)
{
	timeSpentWaiting = _gameRenderer->Render();

	ImGui::UpdatePlatformWindows();
	ImGui::RenderPlatformWindowsDefault();

	return true;
}
