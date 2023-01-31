#include "Application.h"
#include "Game/ECS/Scheduler.h"
#include "Game/Editor/EditorHandler.h"
#include "Game/Gameplay/GameConsole/GameConsole.h"
#include "Game/Rendering/GameRenderer.h"
#include "Game/Rendering/Model/ModelLoader.h"
#include "Game/Scripting/LuaUtil.h"
#include "Game/Util/ServiceLocator.h"

#include <Base/Types.h>
#include <Base/CVarSystem/CVarSystem.h>
#include <Base/Util/Timer.h>
#include <Base/Util/JsonUtils.h>
#include <Base/Util/DebugHandler.h>

#include <imgui/backends/imgui_impl_vulkan.h>
#include <imgui/backends/imgui_impl_glfw.h>
#include <imgui/imguizmo/ImGuizmo.h>
#include <tracy/Tracy.hpp>
#include <enkiTS/TaskScheduler.h>
#include <entt/entt.hpp>

AutoCVar_Int CVAR_FramerateLimit("application.framerateLimit", "enable framerate limit", 1, CVarFlags::EditCheckbox);
AutoCVar_Int CVAR_FramerateLimitTarget("application.framerateLimitTarget", "target framerate while limited", 60);

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
		while (true)
		{
			f32 deltaTime = timer.GetDeltaTime();
			timer.Tick();

			if (!Tick(deltaTime))
				break;

			{
				ServiceLocator::GetGameConsole()->Render(deltaTime);

				/*if (ImGui::BeginMainMenuBar())
				{
					ImVec2 contentRegionAvailable = ImGui::GetContentRegionAvail();

					if (ImGui::BeginMenu("Debug"))
					{
						// Reload shaders button
						if (ImGui::Button("Reload Shaders"))
						{
							_gameRenderer->ReloadShaders(false);
						}
						if (ImGui::Button("Reload Shaders (FORCE)"))
						{
							_gameRenderer->ReloadShaders(true);
						}

						ImGui::EndMenu();
					}

					{
						static char textBuffer[64];
						StringUtils::FormatString(textBuffer, 64, "Fps : %.1f", 1.f / deltaTime);
						ImVec2 fpsTextSize = ImGui::CalcTextSize(textBuffer);

						StringUtils::FormatString(textBuffer, 64, "Ms  : %.2f", deltaTime * 1000);
						ImVec2 msTextSize = ImGui::CalcTextSize(textBuffer);

						f32 textPadding = 10.0f;
						f32 textOffset = (contentRegionAvailable.x - fpsTextSize.x - msTextSize.x) - textPadding;

						ImGui::SameLine(textOffset);
						ImGui::Text("Ms  : %.2f", deltaTime * 1000);
						ImGui::Text("Fps : %.1f", 1.f / deltaTime);
					}

					ImGui::EndMainMenuBar();
				}*/
			}

			_editorHandler->DrawImGuiMenuBar(deltaTime);

			if (!Render(deltaTime))
				break;

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
		std::filesystem::create_directories("Data/configs");

		nlohmann::ordered_json fallback;
		if (JsonUtils::LoadFromPathOrCreate(_cvarJson, fallback, "Data/configs/CVar.json"))
		{
			JsonUtils::LoadJsonIntoCVars(_cvarJson);
			JsonUtils::LoadCVarsIntoJson(_cvarJson);
			JsonUtils::SaveToPath(_cvarJson, "Data/configs/CVar.json");
		}
	}

	_taskScheduler = new enki::TaskScheduler();
	_taskScheduler->Initialize();
	ServiceLocator::SetTaskScheduler(_taskScheduler);

	_registries.gameRegistry = new entt::registry();
	ServiceLocator::SetEnttRegistries(&_registries);

	_gameRenderer = new GameRenderer();
	_modelLoader = new ModelLoader(_gameRenderer->GetModelRenderer());
	_editorHandler = new Editor::EditorHandler();
	ServiceLocator::SetEditorHandler(_editorHandler);
	_modelLoader = new ModelLoader(_gameRenderer->GetModelRenderer());

	_ecsScheduler = new ECS::Scheduler();
	_ecsScheduler->Init(*_registries.gameRegistry);

	ServiceLocator::SetGameConsole(new GameConsole());

	Scripting::LuaUtil::DoString("print(\"Hello World :o\")");

	return true;
}

bool Application::Tick(f32 deltaTime)
{
	bool shouldExit = !_gameRenderer->UpdateWindow(deltaTime);
	if (shouldExit)
		return false;

	// Imgui New Frame
	{
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
				if (!Scripting::LuaUtil::DoString(message.data))
				{
					DebugHandler::PrintError("Failed to run Lua DoString");
				}
				
				break;
			}

			case MessageInbound::Type::Exit:
				return false;

			default: break;
		}
	}

	_ecsScheduler->Update(*_registries.gameRegistry, deltaTime);

	_editorHandler->Update(deltaTime);

	_gameRenderer->UpdateRenderers(deltaTime);

	if (CVarSystem::Get()->IsDirty())
	{
		JsonUtils::LoadCVarsIntoJson(_cvarJson);
		JsonUtils::SaveToPath(_cvarJson, "Data/configs/CVar.json");

		CVarSystem::Get()->ClearDirty();
	}

	return true;
}

bool Application::Render(f32 deltaTime)
{
	_gameRenderer->Render();

	ImGui::UpdatePlatformWindows();
	ImGui::RenderPlatformWindowsDefault();

	return true;
}
