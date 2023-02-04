#pragma once
#include "BaseEditor.h"

namespace Editor
{
	class MapEditor : public BaseEditor
	{
	public:
		MapEditor();

		virtual const char* GetName() override { return "MapEditor"; }

		virtual void DrawImGui() override;

	private:

	private:
	};
}