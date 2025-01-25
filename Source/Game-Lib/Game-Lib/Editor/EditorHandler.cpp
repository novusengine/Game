#include "EditorHandler.h"
#include "ActionStack.h"
#include "AnimationController.h"
#include "AssetBrowser.h"
#include "CameraInfo.h"
#include "CDBEditor.h"
#include "Clock.h"
#include "CVarEditor.h"
#include "EaseCurveTool.h"
#include "Hierarchy.h"
#include "Inspector.h"
#include "MapSelector.h"
#include "NetworkedInfo.h"
#include "PerformanceDiagnostics.h"
#include "SkyboxSelector.h"
#include "TerrainTools.h"
#include "Viewport.h"

#include "Game-Lib/Scripting/LuaManager.h"
#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Base/CVarSystem/CVarSystem.h>

#include <GLFW/glfw3.h>

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <imgui/ImGuiNotify.hpp>
#include <imgui/imguizmo/ImGuizmo.h>
#include <tracy/Tracy.hpp>

#include <fstream>

namespace Editor
{
    AutoCVar_Int CVAR_EditorEnabled(CVarCategory::Client, "editorEnable", "enable editor mode for the client", 1, CVarFlags::EditCheckbox);

    EditorHandler::EditorHandler()
    {
        InputManager* inputManager = ServiceLocator::GetInputManager();
        KeybindGroup* keybindGroup = inputManager->CreateKeybindGroup("GlobalEditor", 0);

        KeybindGroup* imguiKeybindGroup = inputManager->GetKeybindGroupByHash("Imgui"_h);

        _viewport = new Viewport();
        _editors.push_back(_viewport);
        _editors.push_back(new AnimationController());
        _editors.push_back(new CVarEditor());
        _editors.push_back(new CameraInfo());
        _editors.push_back(new CDBEditor());
        _editors.push_back(new Clock());
        _editors.push_back(new PerformanceDiagnostics());
        _editors.push_back(new MapSelector());
        _editors.push_back(new NetworkedInfo());
        _editors.push_back(new SkyboxSelector);
        _editors.push_back(new EaseCurveTool());

        _actionStackEditor = new ActionStackEditor(64);
        _editors.push_back(_actionStackEditor);

        _inspector = new Inspector();
        _editors.push_back(_inspector);

        _viewport->SetInspector(_inspector);
        _inspector->SetViewport(_viewport);

        _hierarchy = new Hierarchy();
        _editors.push_back(_hierarchy);

        _inspector->SetHierarchy(_hierarchy);
        _hierarchy->SetInspector(_inspector);

        _assetBrowser = new AssetBrowser();
        _editors.push_back(_assetBrowser);

        _terrainTools = new TerrainTools();
        _editors.push_back(_terrainTools);
        
        keybindGroup->SetActive(true);

        _editorMode = _viewport->IsEditorMode();
        LoadLayouts();

        for (auto& editor : _editors)
            editor->OnModeUpdate(_editorMode);

        // Bind switch editor keys
        keybindGroup->AddKeyboardCallback("Switch Editor Mode", GLFW_KEY_SPACE, KeybindAction::Press, KeybindModifier::Shift, [this](i32 key, KeybindAction action, KeybindModifier modifier)
        {
            SaveLayout();
            _editorMode = !_editorMode;
            RestoreLayout();

            for (auto& editor : _editors)
                editor->OnModeUpdate(_editorMode);

            ImGui::InsertNotification({ ImGuiToastType::Info, 3000, "Editor: %s", _editorMode ? "Enabled" : "Disabled" });

            _viewport->SetIsEditorMode(_editorMode);

            return true;
        });

        imguiKeybindGroup->AddKeyboardInputValidator("KeyboardInputValidator", [](i32 key, KeybindAction action, KeybindModifier modifier) -> bool
        {
            auto& io = ImGui::GetIO();
            bool wasConsumedByImGui = io.WantCaptureKeyboard;

            return wasConsumedByImGui;
        });

        imguiKeybindGroup->AddMouseInputValidator("MousedInputValidator", [](i32 key, KeybindAction action, KeybindModifier modifier) -> bool
        {
            auto& io = ImGui::GetIO();
            bool wasConsumedByImGui = io.WantCaptureMouse || ImGuizmo::IsOver();

            return wasConsumedByImGui;
        });

        imguiKeybindGroup->AddMousePositionValidator([](f32 x, f32 y) -> bool
        {
            auto& io = ImGui::GetIO();
            bool wasConsumedByImGui = io.MouseHoveredViewport;

            return wasConsumedByImGui;
        });

        imguiKeybindGroup->AddMouseScrollValidator([this](f32 x, f32 y) -> bool
        {
            bool wasConsumedByImGui = _editorMode;

            return wasConsumedByImGui;
        });
    }

    void EditorHandler::NewFrame()
    {

    }

    void EditorHandler::Update(f32 deltaTime)
    {
        ZoneScoped;

        _timeSinceLayoutSave += deltaTime;
        if (_timeSinceLayoutSave > LAYOUT_SAVE_INTERVAL)
        {
            _timeSinceLayoutSave -= LAYOUT_SAVE_INTERVAL;
            SaveLayout();
        }

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
                if (ImGui::Button("Reload Scripts"))
                {
                    ServiceLocator::GetLuaManager()->SetDirty();
                }

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
                    if (editor->IsVisible())
                    {
                        if (ImGui::MenuItem(editor->GetName()))
                        {
                            editor->Show();
                        }
                    }
                    else
                    {
                        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(170, 170, 170, 255));
                        if (ImGui::MenuItem(editor->GetName()))
                        {

                        }

                        if (ImGui::IsItemHovered())
                        {
                            ImGui::BeginTooltip();
                            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
                            ImGui::TextUnformatted("This editor is only available in editor mode.");
                            ImGui::PopTextWrapPos();
                            ImGui::EndTooltip();
                        }
                        ImGui::PopStyleColor();
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

                    ResetLayoutToDefault();
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

    void EditorHandler::ResetLayoutToDefault()
    {
        ImGuiID left;
        ImGuiID right = ImGui::DockBuilderSplitNode(_mainDockID, ImGuiDir_Right, 0.4f, NULL, &left);

        ImGuiID inspector;
        ImGuiID farRight = ImGui::DockBuilderSplitNode(right, ImGuiDir_Right, 0.5f, NULL, &inspector);

        ImGuiID hierarchy;
        ImGuiID belowHierarchy = ImGui::DockBuilderSplitNode(farRight, ImGuiDir_Down, 0.2f, NULL, &hierarchy);

        ImGuiID viewport;
        ImGuiID assetBrowser = ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, 0.3f, NULL, &viewport);

        ImGui::DockBuilderDockWindow(_inspector->GetName(), inspector, 0);
        
        ImGui::DockBuilderDockWindow(_hierarchy->GetName(), hierarchy, 0);
        ImGui::DockBuilderDockWindow("Map", hierarchy, 1);
        ImGui::DockBuilderDockWindow("Performance", hierarchy, 2);
        ImGui::DockBuilderDockWindow("Camera Info", hierarchy, 3);

        ImGui::DockBuilderDockWindow(_actionStackEditor->GetName(), belowHierarchy, 0);

        ImGui::DockBuilderDockWindow(_viewport->GetName(), viewport, 0);

        ImGui::DockBuilderDockWindow(_assetBrowser->GetName(), assetBrowser, 0);

        ImGui::DockBuilderFinish(_mainDockID);

        for (BaseEditor* editor : _editors)
        {
            editor->Reset();
        }
    }

    void EditorHandler::LoadLayouts()
    {
        std::ifstream gameFile("Data/config/Game.layout");
        if (gameFile.good()) 
        {
            _gameLayout.assign(std::istreambuf_iterator<char>(gameFile), std::istreambuf_iterator<char>());

            if (!_editorMode)
            {
                _loadedStartupLayout = true;
            }
        }
        
        std::ifstream editorFile("Data/config/Editor.layout");
        if (editorFile.good()) 
        {
            _editorLayout.assign(std::istreambuf_iterator<char>(editorFile), std::istreambuf_iterator<char>());

            if (_editorMode)
            {
                _loadedStartupLayout = true;
            }
        }
        RestoreLayout();
    }

    void EditorHandler::SaveLayout()
    {
        // Select the layout we want to save to
        std::string& layout = (_editorMode) ? _editorLayout : _gameLayout;

        size_t settingsSize;
        const char* settings = ImGui::SaveIniSettingsToMemory(&settingsSize);

        layout = settings;

        std::string path = "";
        if (_editorMode)
        {
            path = "Data/config/Editor.layout";
        }
        else
        {
            path = "Data/config/Game.layout";
        }

        std::ofstream file(path);
        if (file.is_open())
        {
            file << layout;
            file.close();
        }
    }

    void EditorHandler::RestoreLayout()
    {
        // Select the layout we want to restore from
        std::string& layout = (_editorMode) ? _editorLayout : _gameLayout;

        if (!layout.empty())
        {
            ImGui::LoadIniSettingsFromMemory(layout.data(), layout.size());
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
        if (firstRun && !io.IniFileExisted && !_loadedStartupLayout)
        {
            if (ImGui::DockBuilderGetNode(_mainDockID) != NULL)
            {
                ImGui::DockBuilderRemoveNodeChildNodes(_mainDockID);
            }
            ResetLayoutToDefault();
            firstRun = false;
        }

        if (ImGui::DockBuilderGetNode(_mainDockID) == NULL)
            ResetLayoutToDefault();
    }

    void EditorHandler::EndEditor()
    {
        ImGui::End();
    }
}
