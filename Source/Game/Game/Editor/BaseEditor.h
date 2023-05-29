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
		virtual void Show() { _isVisible = true; };

		virtual void Update(f32 deltaTime) {};

		virtual void BeginImGui() {};

		virtual void DrawImGuiMenuBar() {};
		virtual void DrawImGuiSubMenuBar() {};
		virtual void DrawImGui() {};

		virtual void EndImGui() {};

		bool IsHorizontal();
		bool IsVisible() { return _isVisible; }
		void SetIsVisible(bool isVisible);
		void Reset() { _isVisible = _defaultVisible; }

	protected:
		bool _defaultVisible;
		bool _isVisible;
		bool _lastIsVisible;
	};
}