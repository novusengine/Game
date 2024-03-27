#include "ActionStack.h"
#include "EditorHandler.h"
#include "Inspector.h"
#include "Game/Util/ServiceLocator.h"

#include <Input/InputManager.h>

#include <GLFW/glfw3.h>
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
		: BaseEditor(GetName(), true)
		, _maxSize(maxSize)
	{

		InputManager* inputManager = ServiceLocator::GetInputManager();
		KeybindGroup* keybindGroup = inputManager->GetKeybindGroupByHash("GlobalEditor"_h);

		keybindGroup->AddKeyboardCallback("UndoEditor", GLFW_KEY_Z, KeybindAction::Press, KeybindModifier::Ctrl, [&](i32 key, KeybindAction action, KeybindModifier modifier)
			{
				if (_actionStack.size() == 0)
				return true;

		// Undo most recent action
		BaseAction* lastAction = _actionStack.back();
		lastAction->Undo();
		delete lastAction;

		_actionStack.pop_back();
		ServiceLocator::GetEditorHandler()->GetInspector()->DirtySelection();

		return true;
			});
	}

	void ActionStackEditor::UpdateMode(bool mode)
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