#pragma once
#include "BaseEditor.h"

namespace Editor
{
	class MapSelector : public BaseEditor
	{
	public:
		MapSelector();

		virtual const char* GetName() override { return "Map"; }

		virtual void DrawImGui() override;

	private:
	};
}