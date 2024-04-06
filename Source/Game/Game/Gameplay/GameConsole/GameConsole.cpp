#include "GameConsole.h"
#include "GameConsoleCommandHandler.h"
#include "Game/Util/ServiceLocator.h"

#include <imgui/imgui.h>
#include <imgui/backends/imgui_impl_vulkan.h>
#include <imgui/backends/imgui_impl_glfw.h>
#include <imgui/misc/cpp/imgui_stdlib.h>
#include <imgui/implot.h>

#include <Input/InputManager.h>
#include <Renderer/RenderSettings.h>

#include <GLFW/glfw3.h>

AutoCVar_Int CVAR_GameConsoleEnabled(CVarCategory::Client, "consoleEnabled", "enable game console", 1, CVarFlags::EditReadOnly | CVarFlags::DoNotSave);
AutoCVar_Int CVAR_GameConsoleDuplicateToTerminal(CVarCategory::Client, "consoleDuplicateToTerminal", "enable printing to terminal", 1, CVarFlags::EditCheckbox);

GameConsole::GameConsole()
{
    _lines.push_back("Welcome to Game Console. You may enter 'help' for more information about usage.");

    InputManager* inputManager = ServiceLocator::GetInputManager();
    KeybindGroup* keybindGroup = inputManager->GetKeybindGroupByHash("Debug"_h);
    keybindGroup->AddKeyboardCallback("Enable Game Console", GLFW_KEY_BACKSLASH, KeybindAction::Press, KeybindModifier::Any, [this](i32 key, KeybindAction action, KeybindModifier modifier) -> bool
        {
            Toggle();
            return true;
        });
    keybindGroup->AddKeyboardCallback("Close Game Console", GLFW_KEY_ESCAPE, KeybindAction::Press, KeybindModifier::Any, [this](i32 key, KeybindAction action, KeybindModifier modifier) -> bool
        {
            Disable();
            return false;
        });

    Disable();
    _commandHandler = new GameConsoleCommandHandler();
}

GameConsole::~GameConsole()
{
    delete _commandHandler;
}

void GameConsole::Render(f32 deltaTime)
{
    i32* isGameConsoleEnabled = CVAR_GameConsoleEnabled.GetPtr();
    if (*isGameConsoleEnabled == 0)
        return;

    _visibleProgressTimer = glm::min(_visibleProgressTimer + (5.0f * deltaTime), 1.0f);

    std::string lineToAppend;
    while (_linesToAppend.try_dequeue(lineToAppend))
    {
        _lines.push_back(lineToAppend);
    }

    //f32 heightOffset = ImGui::GetWindowHeight();
    f32 height = glm::mix(0.0f, 300.f, _visibleProgressTimer);

    const ImGuiViewport* viewport = ImGui::GetMainViewport();

    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y /*+ heightOffset */));
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, height));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.14f, 0.14f, 0.14f, 1.0f));
    if (ImGui::Begin("GameConsole", reinterpret_cast<bool*>(isGameConsoleEnabled), ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove))
    {
        ImGui::SetWindowFontScale(1.0f);

        const f32 reservedHeight = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();
        ImGui::BeginChild("TextRegion", ImVec2(0, -reservedHeight), false, ImGuiWindowFlags_HorizontalScrollbar);
        {
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 1));

            for (u32 i = 0; i < _lines.size(); i++)
            {
                const std::string& line = _lines[i];
                ImGui::TextWrapped("%s", line.c_str());
            }

            ImGui::PopStyleVar();
        }
        ImGui::EndChild();
        ImGui::Separator();

        ImGui::PushItemWidth(Renderer::Settings::SCREEN_WIDTH);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.50f, 0.50f, 0.50f, 0.50f));
        bool handleInput = ImGui::InputText("##", &_searchText, ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::PopStyleColor();
        ImGui::PopItemWidth();

        if (handleInput)
        {
            if (_commandHandler->HandleCommand(this, _searchText))
            {
                _lines.push_back(_searchText);
            }

            _searchText = "";
        }

        ImGui::SetItemDefaultFocus();
        if (handleInput)
        {
            ImGui::SetKeyboardFocusHere(-1);
        }
    }
    ImGui::End();
    ImGui::PopStyleColor();
}

void GameConsole::Clear()
{
    _lines.clear();

    std::string dummy;
    while (_linesToAppend.try_dequeue(dummy)) {}
}

void GameConsole::Toggle()
{
    if (CVAR_GameConsoleEnabled.Get())
    {
        Disable();
    }
    else
    {
        Enable();
    }
}

void GameConsole::Enable()
{
    CVAR_GameConsoleEnabled.Set(1);
    _visibleProgressTimer = 0.0f;
}

void GameConsole::Disable()
{
    CVAR_GameConsoleEnabled.Set(0);
}
