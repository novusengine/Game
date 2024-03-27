#pragma once
#include "BaseEditor.h"

#include <Base/Types.h>

namespace Editor
{
	class Inspector;
	
	class Viewport : public BaseEditor
	{
	public:
		Viewport();
		void SetInspector(Inspector* inspector);

		virtual const char* GetName() override { return "Viewport"; }

		virtual void Update(f32 deltaTime) override;
		virtual void OnModeUpdate(bool mode) override;
		virtual void DrawImGui() override;

		void SetIsEditorMode(bool isEditorMode);
		bool IsEditorMode();

		// This will get the correct mouse position, regardless if we're in editor mode or not. Returns true if the mouse position is within the viewport
		bool GetMousePosition(vec2& outMousePos);
		bool IsMouseHoveredOver();

		const vec2& GetViewportPosition() { return _viewportPos; }
		const vec2& GetViewportSize() { return _viewportSize; }
	private:
		void DrawBottomBar(vec2 viewportContentSize);

	private:
		Inspector* _inspector = nullptr;

		vec2 _lastPanelSize = vec2(1920, 1080);

		vec2 _viewportPos = vec2(0, 0);
		vec2 _viewportSize = vec2(0, 0);

		bool _rightClickStartedInViewport = false;
	};
}