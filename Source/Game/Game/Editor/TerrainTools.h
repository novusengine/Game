#pragma once
#include "BaseEditor.h"

#include <imgui/imgui.h>

namespace Editor
{
	class TerrainTools : public BaseEditor
	{
	public:
		enum BrushTypes
		{
			FLAT = 0,
			SMOOTH,
			LINEAR,
			VERTEX,
			COUNT
		};

	public:
		TerrainTools();

		virtual const char* GetName() override { return "Terrain Tools"; }

		virtual void DrawImGui() override;

	private:
		i32 _brushTool = 0;

		f32 _brushHardness = 100.0f;
		f32 _brushRadius = 10.0f;
		f32 _brushPressure = 100.0f;

		Color _vertexColor = Color::Black;
	};
}