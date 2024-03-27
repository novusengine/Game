#pragma once
#include "BaseEditor.h"
#include <deque>

namespace Editor
{
	struct BaseAction
	{
		BaseAction();

		virtual char* GetActionName() = 0;
		virtual void Undo() = 0;
		virtual void Draw() = 0;

		bool isOpen;
	};

	typedef std::deque<BaseAction*> ActionStack;

	class ActionStackEditor : public BaseEditor
	{
	public:
		ActionStackEditor(u32 maxSize);

		virtual const char* GetName() override { return "Action Stack"; }

		virtual void OnModeUpdate(bool mode) override;
		virtual void DrawImGui() override;

		void AddAction(BaseAction* action);

	private:
		u32 _maxSize = 0;
		ActionStack _actionStack;
	};
}