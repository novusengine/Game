#pragma once
#include <Base/Types.h>
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

namespace Editor
{
	class BaseEditor
	{
	public:
		BaseEditor(const char* name, bool defaultVisible);

		void UpdateVisibility();

		virtual const char* GetName() = 0;
		virtual void Show() { _isVisible = true; };

		virtual void Update(f32 deltaTime) {};

		virtual void BeginImGui() {};

		virtual void DrawImGuiMenuBar() {};
		virtual void DrawImGuiSubMenuBar() {};
		virtual void DrawImGui() {};

		virtual void EndImGui() {};

		bool IsHorizontal() { return (ImGui::GetWindowWidth() >= ImGui::GetWindowHeight()); };
		bool IsVisible() { return _isVisible; }
		void SetIsVisible(bool isVisible);
		void Reset() { _isVisible = _defaultVisible; }

	protected:
		bool _defaultVisible;
		bool _isVisible;
		bool _lastIsVisible;
	};
}