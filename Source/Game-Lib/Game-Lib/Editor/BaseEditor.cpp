#include "BaseEditor.h"

#include <Game-Lib/Util/ImguiUtil.h>

#include <Base/CVarSystem/CVarSystem.h>


#include <imgui/imgui.h>

namespace Editor
{
    const CVarCategory CVAR_CATEGORY = CVarCategory::Client;

    BaseEditor::BaseEditor(const char* name, BaseEditorFlags_t flags)
    {
        CVarSystem* cvarSystem = CVarSystem::Get();

        _flags = flags;

        std::string cvarName = name;
        cvarName.append("IsOpen");
        std::string::iterator end_pos = std::remove(cvarName.begin(), cvarName.end(), ' ');
        cvarName.erase(end_pos, cvarName.end());

        CVarParameter* cvar = cvarSystem->GetCVar(CVAR_CATEGORY, cvarName.c_str());
        if (cvar == nullptr)
        {
            bool isDefaultVisible = (_flags & BaseEditorFlags_DefaultVisible) != 0;
            cvarSystem->CreateIntCVar(CVAR_CATEGORY, cvarName.c_str(), "Is window open", isDefaultVisible, isDefaultVisible, CVarFlags::RuntimeCreated | CVarFlags::Hidden);
            
            _isVisible = isDefaultVisible;
            cvarSystem->MarkDirty();
            return;
        }

        bool isVisible = *cvarSystem->GetIntCVar(CVAR_CATEGORY, cvarName.c_str());
        _isVisible = isVisible;
        _lastIsVisible = isVisible;
    }

    void BaseEditor::UpdateVisibility()
    {
        if (_isVisible != _lastIsVisible)
        {
            CVarSystem* cvarSystem = CVarSystem::Get();

            std::string cvarName = GetName();
            cvarName.append("IsOpen");
            std::string::iterator end_pos = std::remove(cvarName.begin(), cvarName.end(), ' ');
            cvarName.erase(end_pos, cvarName.end());
            
            cvarSystem->SetIntCVar(CVAR_CATEGORY, cvarName.c_str(), _isVisible);
            cvarSystem->MarkDirty();
        }
        _lastIsVisible = _isVisible;
    }

    void BaseEditor::Show()
    {
        _isVisible = true;
    }

    void BaseEditor::Hide()
    {
        _isVisible = false;
    }

    bool BaseEditor::OpenMenu(const char* title)
    {
        std::string menuName = std::string("Menu" + std::string(GetName()));

        if (OpenRightClickMenu())
        {
            ImGui::OpenPopup(menuName.c_str());
        }

        if (ImGui::BeginPopup(menuName.c_str()))
        {
            ImGui::Text("%s", title);
            ImGui::Separator();

            return true;
        }

        return false;
    }

    void BaseEditor::CloseMenu()
    {
        ImGui::EndPopup();
    }

    bool BaseEditor::OpenRightClickMenu()
    {
        return (IsMouseInsideWindow() && ImGui::IsMouseReleased(ImGuiMouseButton_Right));
    }

    // This is for be 100% sure the mouse is inside the desired window
    // like that only desired code is called
    bool BaseEditor::IsMouseInsideWindow()
    {
        ImVec2 mouse = ImGui::GetMousePos();
        auto mouseX = mouse.x;
        auto mouseY = mouse.y;
        ImVec2 windowPos = ImGui::GetWindowPos();
        ImVec2 windowSize = ImGui::GetWindowSize();

        return (mouseX >= windowPos.x && mouseX <= windowPos.x + windowSize.x &&
            mouseY >= windowPos.y && mouseY <= windowPos.y + windowSize.y &&
            ImGui::IsWindowHovered());
    }

    bool BaseEditor::IsHiddenInMenuBar()
    {
        bool isHiddenInMenuBar = (_flags & BaseEditorFlags_HideInMenuBar) != 0;
        return isHiddenInMenuBar;
    }

    bool BaseEditor::IsEditorOnly()
    {
        bool isEditorOnly = (_flags & BaseEditorFlags_EditorOnly) != 0;
        return isEditorOnly;
    }

    bool BaseEditor::IsHorizontal()
    {
        return (ImGui::GetWindowWidth() >= ImGui::GetWindowHeight());
    }

    bool& BaseEditor::IsVisible()
    {
        return _isVisible;
    }

    void BaseEditor::SetIsVisible(bool isVisible)
    {
        _isVisible = isVisible;
    }

    void BaseEditor::Reset()
    {
        bool defaultVisible = (_flags & BaseEditorFlags_DefaultVisible) != 0;
        _isVisible = defaultVisible;
    }
}