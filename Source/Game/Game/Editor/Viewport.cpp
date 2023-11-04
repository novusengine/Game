#include "Viewport.h"

#include <Game/Application/EnttRegistries.h>
#include <Game/Rendering/GameRenderer.h>
#include <Game/Rendering/Debug/DebugRenderer.h>
#include <Game/Rendering/Camera.h>
#include <Game/Util/ServiceLocator.h>
#include <Game/ECS/Singletons/EngineStats.h>
#include <Game/ECS/Singletons/FreeflyingCameraSettings.h>
#include <Game/ECS/Components/Camera.h>
#include <Game/ECS/Systems/FreeflyingCamera.h>
#include <Game/ECS/Util/CameraUtil.h>
#include <Game/Util/ImguiUtil.h>
#include <Game/Editor/Inspector.h>

#include <Base/CVarSystem/CVarSystem.h>

#include <Input/InputManager.h>

#include <Renderer/RenderSettings.h>

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <imgui/imguizmo/ImGuizmo.h>

AutoCVar_Int CVAR_IsEditorMode("editor.isEditorMode", "enable editor mode", 0, CVarFlags::EditCheckbox);

namespace Editor
{
	Viewport::Viewport()
		: BaseEditor(GetName(), true)
	{
		_lastPanelSize = vec2(Renderer::Settings::SCREEN_WIDTH, Renderer::Settings::SCREEN_HEIGHT);
	}

	void Viewport::SetInspector(Inspector* inspector)
	{
		_inspector = inspector;
	}

	void Viewport::Update(f32 deltaTime)
	{
		if (!IsEditorMode())
			return;

		// Create the viewport here only so we can check ContentRegionAvail and flag a resize
		ImGui::SetNextWindowSize(_lastPanelSize, ImGuiCond_FirstUseEver);

		if (ImGui::Begin(GetName(), &IsVisible()))
		{
			Renderer::Renderer* renderer = ServiceLocator::GetGameRenderer()->GetRenderer();

			vec2 viewportPanelSize = ImGui::GetContentRegionAvail();
			if (viewportPanelSize.x != _lastPanelSize.x || viewportPanelSize.y != _lastPanelSize.y)
			{
				renderer->SetRenderSize(viewportPanelSize);
				_lastPanelSize = viewportPanelSize;

				ImDrawList* drawList = ImGui::GetWindowDrawList();
				drawList->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
			}
		}
		ImGui::End();
	}

	void Viewport::DrawImGui()
	{
		if (!IsEditorMode())
			return;

		if (ImGui::Begin(GetName(), &IsVisible()))
		{
			GameRenderer* gameRenderer = ServiceLocator::GetGameRenderer();
			Renderer::Renderer* renderer = gameRenderer->GetRenderer();
			
			f32 bottomBarHeight = ImGui::GetFrameHeightWithSpacing();//ImGui::GetTextLineHeightWithSpacing();
			
			vec2 windowPos = ImGui::GetWindowPos();
			vec2 contentRegionAvail = ImGui::GetContentRegionAvail();
			contentRegionAvail.y -= bottomBarHeight;

			ImGuizmo::SetRect(windowPos.x, windowPos.y, contentRegionAvail.x, contentRegionAvail.y);
			ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());

			RenderResources& resources = gameRenderer->GetRenderResources();
			void* imageHandle = renderer->GetImguiImageHandle(resources.sceneColor);
			ImGui::Image(imageHandle, contentRegionAvail, vec2(0, 0), vec2(1, 1));

			vec2 viewportMin = ImGui::GetItemRectMin();
			vec2 viewportMax = ImGui::GetItemRectMax();

			_viewportPos = viewportMin;
			_viewportSize = viewportMax - viewportMin;

			entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
			entt::registry::context& ctx = registry->ctx();

			ECS::Singletons::FreeflyingCameraSettings& cameraSettings = ctx.get<ECS::Singletons::FreeflyingCameraSettings>();

			ImGuiIO& io = ImGui::GetIO();

			if (ImGui::IsItemClicked(0)) // Left click
			{
				KeybindModifier modifier = KeybindModifier::None;

				if (io.KeyShift)
					modifier |= KeybindModifier::Shift;
				else if (io.KeyCtrl)
					modifier |= KeybindModifier::Ctrl;
				else if (io.KeyAlt)
					modifier |= KeybindModifier::Alt;

				_inspector->OnMouseClickLeft(0, KeybindAction::Click, modifier);
			}

			if (ImGui::IsMouseDoubleClicked(1)) // Double right click
			{
				ECS::Util::CameraUtil::SetCaptureMouse(true);

				vec2 capturePos = windowPos + (contentRegionAvail / 2.0f);//vec2(windowPos.x + (contentRegionAvail.x / 2.0f), windowPos.y + (contentRegionAvail.y / 2.0f));
				cameraSettings.prevMousePosition = capturePos;
			}
			else if (ImGui::IsItemClicked(1)) // Right click
			{
				cameraSettings.prevMousePosition = io.MousePos;
			}
			else
			{
				// If holding down rightclick
				if (io.MouseDown[1])
				{
					ECS::Systems::FreeflyingCamera::CapturedMouseMoved(*registry, io.MousePos);
				}
				// If we scrolled
				if (io.MouseWheel != 0.0f)
				{
					ECS::Systems::FreeflyingCamera::CapturedMouseScrolled(*registry, vec2(0.0f, io.MouseWheel));
				}
			}

			DrawBottomBar(contentRegionAvail);
		}
		ImGui::End();
	}

	void Viewport::SetIsEditorMode(bool isEditorMode)
	{
		CVAR_IsEditorMode.Set(isEditorMode);
	}

	bool Viewport::IsEditorMode()
	{
		return CVAR_IsEditorMode.Get() == 1;
	}

	bool Viewport::GetMousePosition(vec2& outMousePos)
	{
		InputManager* inputManager = ServiceLocator::GetInputManager();

		if (IsEditorMode())
		{
			vec2 mousePos = ImGui::GetMousePos();

			vec2 viewportPos = GetViewportPosition();
			mousePos -= viewportPos;

			vec2 viewportSize = GetViewportSize();
			if (mousePos.x < 0 || mousePos.y < 0 || mousePos.x >= viewportSize.x || mousePos.y >= viewportSize.y)
			{
				outMousePos = vec2(0, 0);
				return false;
			}

			outMousePos = mousePos;
		}
		else
		{
			vec2 mousePosition = inputManager->GetMousePosition();
			outMousePos = mousePosition;
		}

		return true;
	}

	void Viewport::DrawBottomBar(vec2 viewportContentSize)
	{
		if (ImGui::BeginChild("BottomBar"))
		{
			i32 width = static_cast<i32>(viewportContentSize.x);
			i32 height = static_cast<i32>(viewportContentSize.y);
			ImGui::Text("Render Resolution: %ix%i", width, height);

			entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
			entt::registry::context& ctx = registry->ctx();
			ECS::Singletons::FreeflyingCameraSettings& settings = ctx.get<ECS::Singletons::FreeflyingCameraSettings>();
			ImGui::SameLine();
			ImGui::Text("Camera Speed: %.1f", settings.cameraSpeed);
		}
		ImGui::EndChild();
	}
}