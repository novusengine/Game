#include "Viewport.h"

#include <Game-Lib/Application/EnttRegistries.h>
#include <Game-Lib/Rendering/GameRenderer.h>
#include <Game-Lib/Rendering/Debug/DebugRenderer.h>
#include <Game-Lib/Rendering/Camera.h>
#include <Game-Lib/Util/ServiceLocator.h>
#include <Game-Lib/ECS/Singletons/EngineStats.h>
#include <Game-Lib/ECS/Singletons/FreeflyingCameraSettings.h>
#include <Game-Lib/ECS/Components/Camera.h>
#include <Game-Lib/ECS/Systems/FreeflyingCamera.h>
#include <Game-Lib/ECS/Util/CameraUtil.h>
#include <Game-Lib/Util/ImguiUtil.h>
#include <Game-Lib/Editor/Inspector.h>

#include <Base/CVarSystem/CVarSystem.h>

#include <Input/InputManager.h>

#include <Renderer/RenderSettings.h>

#include <GLFW/glfw3.h>

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <imgui/imguizmo/ImGuizmo.h>

AutoCVar_Int CVAR_IsEditorMode(CVarCategory::Client, "isEditorMode", "enable editor mode", 0, CVarFlags::Hidden);

namespace Editor
{
    Viewport::Viewport()
        : BaseEditor(GetName(), BaseEditorFlags_DefaultVisible | BaseEditorFlags_EditorOnly)
    {
        _lastPanelSize = vec2(Renderer::Settings::SCREEN_WIDTH, Renderer::Settings::SCREEN_HEIGHT);

        InputManager* inputManager = ServiceLocator::GetInputManager();
        KeybindGroup* imguiKeybindGroup = inputManager->GetKeybindGroupByHash("Imgui"_h);

        imguiKeybindGroup->AddKeyboardCallback("Editor RightClick", GLFW_MOUSE_BUTTON_RIGHT, KeybindAction::Press, KeybindModifier::Any, [this](i32 key, KeybindAction action, KeybindModifier modifier)
        {
            InputManager* inputManager = ServiceLocator::GetInputManager();
            entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
            entt::registry::context& ctx = registry->ctx();

            auto& cameraSettings = ctx.get<ECS::Singletons::FreeflyingCameraSettings>();
            if (cameraSettings.captureMouse)
                return false;

            cameraSettings.captureMouseHasMoved = false;

            ImGuiContext* context = ImGui::GetCurrentContext();
            _rightClickStartedInViewport = context->HoveredWindow && strcmp(context->HoveredWindow->Name, GetName()) == 0;
            return true;
        });

        imguiKeybindGroup->AddMouseScrollCallback([this](f32 x, f32 y) -> bool
        {
            ImGuiContext* context = ImGui::GetCurrentContext();

            if (context->HoveredWindow)
            {
                InputManager* inputManager = ServiceLocator::GetInputManager();
                KeybindGroup* imguiKeybindGroup = inputManager->GetKeybindGroupByHash("Imgui"_h);

                bool isViewportHovered = strcmp(context->HoveredWindow->Name, GetName()) == 0;
                bool altIsDown = ImGui::GetIO().KeyAlt;

                if (isViewportHovered && altIsDown)
                {
                    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
                    ECS::Systems::FreeflyingCamera::CapturedMouseScrolled(*registry, vec2(x, y));
                }
            }

            return true;
        });
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

    void Viewport::OnModeUpdate(bool mode)
    {
        SetIsVisible(mode);
    }

    void Viewport::DrawImGui()
    {
        if (!IsEditorMode())
            return;

        if (ImGui::Begin(GetName(), &IsVisible()))
        {
            InputManager* inputManager = ServiceLocator::GetInputManager();
            KeybindGroup* imguiKeybindGroup = inputManager->GetKeybindGroupByHash("Imgui"_h);

            GameRenderer* gameRenderer = ServiceLocator::GetGameRenderer();
            Renderer::Renderer* renderer = gameRenderer->GetRenderer();
            
            f32 bottomBarHeight = ImGui::GetFrameHeightWithSpacing();//ImGui::GetTextLineHeightWithSpacing();
            
            vec2 windowPos = ImGui::GetWindowPos();
            vec2 contentRegionAvail = ImGui::GetContentRegionAvail();
            contentRegionAvail.y -= bottomBarHeight;

            ImGuizmo::SetRect(windowPos.x, windowPos.y, contentRegionAvail.x, contentRegionAvail.y);
            ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());

            RenderResources& resources = gameRenderer->GetRenderResources();
            ImTextureID textureID = renderer->GetImguiTextureID(resources.sceneColor);
            ImGui::Image(textureID, contentRegionAvail, vec2(0, 0), vec2(1, 1));

            vec2 viewportMin = ImGui::GetItemRectMin();
            vec2 viewportMax = ImGui::GetItemRectMax();

            _viewportPos = viewportMin;
            _viewportSize = viewportMax - viewportMin;

            entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
            entt::registry::context& ctx = registry->ctx();

            auto& cameraSettings = ctx.get<ECS::Singletons::FreeflyingCameraSettings>();

            ImGuiIO& io = ImGui::GetIO();

            if (ImGui::IsMouseDoubleClicked(1) && ImGui::IsItemHovered()) // Double right click
            {
                ECS::Util::CameraUtil::SetCaptureMouse(true);

                cameraSettings.prevMousePosition = inputManager->GetMousePosition();
                cameraSettings.captureMouseHasMoved = false;
            }
            else if (imguiKeybindGroup->IsKeybindPressed("Editor RightClick"_h))
            {
                if (_rightClickStartedInViewport)
                {
                    vec2 mousePos = inputManager->GetMousePosition();
                    ECS::Systems::FreeflyingCamera::CapturedMouseMoved(*registry, mousePos);
                }
            }
            else
            {
                _rightClickStartedInViewport = false;

                if (!cameraSettings.captureMouse)
                    cameraSettings.captureMouseHasMoved = false;
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

    bool Viewport::IsMouseHoveredOver()
    {
        vec2 mousePos = ImGui::GetMousePos();

        vec2 viewportPos = GetViewportPosition();
        mousePos -= viewportPos;

        vec2 viewportSize = GetViewportSize();
        if (mousePos.x < 0 || mousePos.y < 0 || mousePos.x >= viewportSize.x || mousePos.y >= viewportSize.y)
        {
            return false;
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
            auto& settings = ctx.get<ECS::Singletons::FreeflyingCameraSettings>();

            ImGui::SameLine();
            ImGui::Text("Camera Speed: %.1f", settings.cameraSpeed);
        }
        ImGui::EndChild();
    }
}
