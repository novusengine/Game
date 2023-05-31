#include "BaseEditor.h"

#include <Game/Util/ImguiUtil.h>

#include <Base/CVarSystem/CVarSystem.h>


#include <imgui/imgui.h>

namespace Editor
{
	BaseEditor::BaseEditor(const char* name, bool defaultVisible)
		: _defaultVisible(defaultVisible)
	{
		CVarSystem* cvarSystem = CVarSystem::Get();

		std::string cvarName = "editor.";
		cvarName.append(name);
		cvarName.append(".isOpen");
		CVarParameter* cvar = cvarSystem->GetCVar(cvarName.c_str());

		if (cvar == nullptr)
		{
			cvarSystem->CreateIntCVar(cvarName.c_str(), "Is window open", defaultVisible, defaultVisible);
			
			_isVisible = defaultVisible;
			cvarSystem->MarkDirty();
			return;
		}

		bool isVisible = *cvarSystem->GetIntCVar(cvarName.c_str());
		_isVisible = isVisible;
		_lastIsVisible = isVisible;
	}

	void BaseEditor::UpdateVisibility()
	{
		if (_isVisible != _lastIsVisible)
		{
			CVarSystem* cvarSystem = CVarSystem::Get();

			std::string cvarName = "editor.";
			cvarName.append(GetName());
			cvarName.append(".isOpen");
			//CVarParameter* cvar = cvarSystem->GetCVar(cvarName.c_str());

			cvarSystem->SetIntCVar(cvarName.c_str(), _isVisible);
			cvarSystem->MarkDirty();
		}
		_lastIsVisible = _isVisible;
	}

	void BaseEditor::Show()
	{
		_isVisible = true;
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
		_isVisible = _defaultVisible;
	}
}