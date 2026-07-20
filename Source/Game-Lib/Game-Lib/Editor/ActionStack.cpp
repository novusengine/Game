#include "ActionStack.h"
#include "EditorHandler.h"
#include "Game-Lib/Input/InputActionSystem.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <imgui/imgui.h>

namespace Editor
{
    BaseAction::BaseAction()
    {
        EditorHandler* editorHandler = ServiceLocator::GetEditorHandler();

        ActionStackEditor* actionStackEditor = editorHandler->GetActionStackEditor();
        actionStackEditor->AddAction(this);
    }

    ActionStackEditor::ActionStackEditor(u32 maxSize)
        : BaseEditor(GetName(), BaseEditorFlags_DefaultVisible | BaseEditorFlags_EditorOnly)
        , _maxSize(maxSize)
    {

        InputActionSystem* inputActions = ServiceLocator::GetInputActionSystem();
        inputActions->RegisterAction("Editor"_x, "UndoEditor", "Undo", "Editor",
            InputBinding::Keyboard(Key::Z, InputModifier::Control, ModifierMatch::AtLeast), [this](const InputActionEvent& event)
        {
            if (event.phase != InputPhase::Pressed)
                return InputReply::Handled;

            if (_actionStack.empty())
                return InputReply::Consumed;

            BaseAction* lastAction = _actionStack.back();
            lastAction->Undo();
            delete lastAction;

            _actionStack.pop_back();

            return InputReply::Consumed;
        });
    }

    void ActionStackEditor::OnModeUpdate(bool mode)
    {
        SetIsVisible(mode);
    }

    void ActionStackEditor::DrawImGui()
    {
        if (ImGui::Begin(GetName(), &IsVisible()))
        {
            if (_actionStack.size() > 0)
            {
                for (i32 i = static_cast<u32>(_actionStack.size()) - 1; i >= 0; i--)
                {
                    BaseAction* action = _actionStack[i];
                    if (ImGui::Selectable(action->GetActionName(), action->isOpen))
                    {
                        action->isOpen = !action->isOpen;
                    }
                    if (action->isOpen)
                    {
                        action->Draw();
                    }
                }
            }
        }
        ImGui::End();
    }

    void ActionStackEditor::AddAction(BaseAction* action)
    {
        _actionStack.push_back(action);

        while (_actionStack.size() > _maxSize)
        {
            _actionStack.pop_front();
            // TODO: Merge popped actions into a "full state" or whatever
        }
    }

}
