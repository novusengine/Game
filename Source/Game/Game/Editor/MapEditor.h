#pragma once
#include "BaseEditor.h"

namespace Editor
{
	class MapEditor : public BaseEditor
	{
	public:
		MapEditor();

		virtual const char* GetName() override { return "Map"; }

		virtual void DrawImGui() override;

	private:
		bool _hasLoadedMap = false;

	private:
	};
}