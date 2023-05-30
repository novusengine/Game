#pragma once
#include <Base/Types.h>

namespace Editor
{
	class BaseEditor
	{
	public:
		BaseEditor(const char* name, bool defaultVisible);

		void UpdateVisibility();

		virtual const char* GetName() = 0;
		virtual void Show();

		virtual void Update(f32 deltaTime) {};

		virtual void BeginImGui() {};

		virtual void DrawImGuiMenuBar() {};
		virtual void DrawImGuiSubMenuBar() {};
		virtual void DrawImGui() {};

		virtual void EndImGui() {};

		bool IsHorizontal();
		bool& IsVisible();
		void SetIsVisible(bool isVisible);
		void Reset();

	protected:
		bool _isVisible;
		bool _lastIsVisible;
		bool _defaultVisible;
	};
}