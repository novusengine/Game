#include "EditorHandler.h"
#include "CVarEditor.h"
#include "CameraInfo.h"
#include "PerformanceDiagnostics.h"

#include "Game/Util/ServiceLocator.h"
#include "Game/Rendering/GameRenderer.h"

#include <Base/CVarSystem/CVarSystem.h>

#include <tracy/Tracy.hpp>
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <imgui/imgui_notify.h>

namespace Editor
{
    AutoCVar_Int CVAR_EditorEnabled("editor.Enable", "enable editor mode for the client", 1, CVarFlags::EditCheckbox);

    EditorHandler::EditorHandler()
    {
        InputManager* inputManager = ServiceLocator::GetGameRenderer()->GetInputManager();
        KeybindGroup* keybindGroup = inputManager->CreateKeybindGroup("GlobalEditor", 0);

        _editors.push_back(new CVarEditor());
        _editors.push_back(new CameraInfo());
        _editors.push_back(new PerformanceDiagnostics());
        
        keybindGroup->SetActive(true);

        // Bind switch editor keys
        keybindGroup->AddKeyboardCallback("Switch Editor Mode", GLFW_KEY_SPACE, KeybindAction::Press, KeybindModifier::Shift, [this](i32 key, KeybindAction action, KeybindModifier modifier)
        {
            _editorMode = !_editorMode;

            ImGui::InsertNotification({ ImGuiToastType_Info, 3000, "Editor: %s", _editorMode ? "Enabled" : "Disabled" });
            return true;
        });
    }

    void EditorHandler::Update(f32 deltaTime)
    {
        ZoneScoped;

        for (BaseEditor* editor : _editors)
        {
            editor->Update(deltaTime);
            editor->UpdateVisibility();
        }
    }

    void EditorHandler::BeginImGui()
    {
        ZoneScoped;

        for (BaseEditor* editor : _editors)
        {
            editor->BeginImGui();
        }
    }

    void EditorHandler::DrawImGuiMenuBar(f32 deltaTime)
    {
        ZoneScoped;
        GameRenderer* gameRenderer = ServiceLocator::GetGameRenderer();

        if (ImGui::BeginMainMenuBar())
        {
            ImVec2 contentRegionAvailable = ImGui::GetContentRegionAvail();

            if (ImGui::BeginMenu("Debug"))
            {
                // Reload shaders button
                if (ImGui::Button("Reload Shaders"))
                {
                    gameRenderer->ReloadShaders(false);
                }
                if (ImGui::Button("Reload Shaders (FORCE)"))
                {
                    gameRenderer->ReloadShaders(true);
                }

                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Window"))
            {
                for (BaseEditor* editor : _editors)
                {
                    if (ImGui::MenuItem(editor->GetName()))
                    {
                        editor->Show();
                    }
                }

                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Editor"))
            {
                for (BaseEditor* editor : _editors)
                {
                    editor->DrawImGuiSubMenuBar();
                }

                ImGui::EndMenu();
            }

            for (BaseEditor* editor : _editors)
            {
                editor->DrawImGuiMenuBar();
            }

            if (ImGui::BeginMenu("Help"))
            {
                if (ImGui::MenuItem("Reset Layout"))
                {
                    if (ImGui::DockBuilderGetNode(_mainDockID) != NULL)
                    {
                        ImGui::DockBuilderRemoveNodeChildNodes(_mainDockID);
                    }

                    ResetLayout();
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

    void EditorHandler::DrawImGui()
    {
        ZoneScoped;

        for (BaseEditor* editor : _editors)
        {
            if (!editor->IsVisible())
                continue;

            editor->DrawImGui();
        }
    }

    void EditorHandler::EndImGui()
    {
        ZoneScoped;

        for (BaseEditor* editor : _editors)
        {
            editor->EndImGui();
        }

        // Render toasts on top of everything, at the end of your code!
        // You should push style vars here
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 5.f); // Round borders
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(43.f / 255.f, 43.f / 255.f, 43.f / 255.f, 100.f / 255.f)); // Background color
        ImGui::RenderNotifications(); // <-- Here we render all notifications
        ImGui::PopStyleVar(1); // Don't forget to Pop()
        ImGui::PopStyleColor(1);
    }

    void EditorHandler::ResetLayout()
    {
        ImGuiID left;
        ImGuiID right = ImGui::DockBuilderSplitNode(_mainDockID, ImGuiDir_Right, 0.4f, NULL, &left);

        ImGuiID hierarchy;
        ImGuiID inspector = ImGui::DockBuilderSplitNode(right, ImGuiDir_Right, 0.5f, NULL, &hierarchy);

        ImGuiID belowInspector = ImGui::DockBuilderSplitNode(inspector, ImGuiDir_Down, 0.2f, NULL, &inspector);

        ImGuiID viewport;
        ImGuiID bottom = ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, 0.4f, NULL, &viewport);

        ImGui::DockBuilderDockWindow("Engine Info", inspector, 0);
        //ImGui::DockBuilderDockWindow(_inspector->GetName(), inspector, 1);
        //ImGui::DockBuilderDockWindow(_actionStack->GetName(), belowInspector, 0);
        ImGui::DockBuilderDockWindow("Clock", belowInspector, 1);
        //ImGui::DockBuilderDockWindow(_hierarchy->GetName(), hierarchy);
        //ImGui::DockBuilderDockWindow(_viewport->GetName(), viewport);
        ImGui::DockBuilderDockWindow("Bottom", bottom);

        ImGui::DockBuilderFinish(_mainDockID);

        for (BaseEditor* editor : _editors)
        {
            editor->Reset();
        }
    }

    void EditorHandler::BeginEditor()
    {
        ImGuiIO& io = ImGui::GetIO();

        static ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;
        static bool opt_padding = false;
        static bool opt_fullscreen = true;

        // We are using the ImGuiWindowFlags_NoDocking flag to make the parent window not dockable into,
        // because it would be confusing to have two docking targets within each others.
        ImGuiWindowFlags window_flags = 0;///*ImGuiWindowFlags_MenuBar |*/ ImGuiWindowFlags_NoDocking;
        if (opt_fullscreen)
        {
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->WorkPos);
            ImGui::SetNextWindowSize(viewport->WorkSize);
            ImGui::SetNextWindowViewport(viewport->ID);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
            window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
        }
        else
        {
            dockspace_flags &= ~ImGuiDockNodeFlags_PassthruCentralNode;
        }

        if (!_editorMode)
        {
            dockspace_flags |= ImGuiDockNodeFlags_PassthruCentralNode;
        }

        // When using ImGuiDockNodeFlags_PassthruCentralNode, DockSpace() will render our background
        // and handle the pass-thru hole, so we ask Begin() to not render a background.
        if (dockspace_flags & ImGuiDockNodeFlags_PassthruCentralNode)
        {
            window_flags |= ImGuiWindowFlags_NoBackground;
        }

        // Important: note that we proceed even if Begin() returns false (aka window is collapsed).
        // This is because we want to keep our DockSpace() active. If a DockSpace() is inactive,
        // all active windows docked into it will lose their parent and become undocked.
        // We cannot preserve the docking relationship between an active window and an inactive docking, otherwise
        // any change of dockspace/settings would lead to windows being stuck in limbo and never being visible.
        if (!opt_padding)
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

        bool isOpen = true;
        ImGui::Begin("Editor", &isOpen, window_flags);
        if (!opt_padding)
            ImGui::PopStyleVar();

        if (opt_fullscreen)
            ImGui::PopStyleVar(2);

        // Submit the DockSpace
        if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
        {
            _mainDockID = ImGui::GetID("Main");
            ImGui::DockSpace(_mainDockID, ImVec2(0.0f, 0.0f), dockspace_flags);
        }

        static bool firstRun = true;
        if (firstRun && !io.IniFileExisted)
        {
            if (ImGui::DockBuilderGetNode(_mainDockID) != NULL)
            {
                ImGui::DockBuilderRemoveNodeChildNodes(_mainDockID);
            }
            ResetLayout();
            firstRun = false;
        }

        if (ImGui::DockBuilderGetNode(_mainDockID) == NULL)
            ResetLayout();
    }

    void EditorHandler::EndEditor()
    {
        ImGui::End();
    }
}