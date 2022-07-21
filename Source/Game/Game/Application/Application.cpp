#include "Application.h"
#include "Game/Rendering/GameRenderer.h"
#include "Game/Util/ServiceLocator.h"

#include <Base/Types.h>
#include <Base/Util/Timer.h>
#include <Base/Util/DebugHandler.h>

#include <imgui/backends/imgui_impl_vulkan.h>
#include <imgui/backends/imgui_impl_glfw.h>
#include <imgui/imguizmo/ImGuizmo.h>
#include <tracy/Tracy.hpp>

#include "Game/Rendering/Model/ModelRenderer.h"
#include <Base/Memory/FileReader.h>
#include <Base/Memory/Bytebuffer.h>
#include <FileFormat/Models/Model.h>
#include <filesystem>
namespace fs = std::filesystem;

Application::Application() : _messagesInbound(256), _messagesOutbound(256) { }
Application::~Application() 
{
	delete _gameRenderer;
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

	DebugHandler::PrintSuccess("Application : Shutdown Initiated");
	Cleanup();
	DebugHandler::PrintSuccess("Application : Shutdown Complete");

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

				if (ImGui::BeginMainMenuBar())
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
				}
			}

			if (!Render(deltaTime))
				break;

			constexpr f32 targetFramerate = 240.0f;
			constexpr f32 targetDelta = 1.0f / targetFramerate;

			for (deltaTime = timer.GetDeltaTime(); deltaTime < targetDelta; deltaTime = timer.GetDeltaTime())
			{
				std::this_thread::yield();
			}

			FrameMark;
		}
	}

	Stop();
}

bool Application::Init()
{
	_gameRenderer = new GameRenderer();
	ServiceLocator::SetGameRenderer(_gameRenderer);

	/*
		std::string modelPath = "Models/Dragon_High.model";
		fs::path absoluteModelPath = fs::absolute(modelPath);
	
		std::string modelPathStr = absoluteModelPath.string();
		std::string modelFileNameStr = absoluteModelPath.filename().string();
	
		FileReader reader(modelPathStr, modelFileNameStr);
		if (reader.Open())
		{
			size_t bufferSize = reader.Length();
	
			std::shared_ptr<Bytebuffer> buffer = Bytebuffer::BorrowRuntime(bufferSize);
			reader.Read(buffer.get(), bufferSize);
	
			Model::Header* header = reinterpret_cast<Model::Header*>(buffer->GetDataPointer());
	
			_gameRenderer->GetModelRenderer()->AddModel(*header, buffer, vec3(0.0f, 0.0f, 0.0f), quat(1.0f, 0.0f, 0.0f, 0.0f), vec3(1.0f, 1.0f, 1.0f));
		}
	*/

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

	MessageInbound message;
	while (_messagesInbound.try_dequeue(message))
	{
		assert(message.type != MessageInbound::Type::Invalid);

		switch (message.type)
		{
			case MessageInbound::Type::Print:
			{
				DebugHandler::PrintSuccess(message.data);
				break;
			}

			case MessageInbound::Type::Ping:
			{
				MessageOutbound pongMessage(MessageOutbound::Type::Pong);
				_messagesOutbound.enqueue(pongMessage);

				DebugHandler::PrintSuccess("Main Thread -> Application Thread : Ping");
				break;
			}

			case MessageInbound::Type::Exit:
				return false;

			default: break;
		}
	}

	_gameRenderer->UpdateRenderers(deltaTime);

	return true;
}

bool Application::Render(f32 deltaTime)
{
	_gameRenderer->Render();

	ImGui::UpdatePlatformWindows();
	ImGui::RenderPlatformWindowsDefault();

	return true;
}
