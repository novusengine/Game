#pragma once
#include "BaseEditor.h"

#include <imgui/imgui.h>

namespace Editor
{
	class TerrainTools : public BaseEditor
	{
	public:
		TerrainTools();

		virtual const char* GetName() override { return "Terrain Tools"; }

		virtual void DrawImGui() override;

	private:
		f32 _brushHardness = 100.0f;
		f32 _brushRadius = 10.0f;
		f32 _brushPressure = 100.0f;

		ImVec4 _vertexColor = { 0.0f, 0.0f, 0.0f, 1.0f };
	};
}