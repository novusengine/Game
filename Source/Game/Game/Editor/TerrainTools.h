#pragma once
#include "BaseEditor.h"

#include <imgui/imgui.h>

#include <Base/Math/Color.h>


namespace Editor
{
	class TerrainTools : public BaseEditor
	{
	public:
		enum class BrushTypes
		{
			FLAT = 0,
			SMOOTH,
			LINEAR,
			VERTEX,
			COUNT
		};

		enum class HardnessMode
		{
			LINEAR = 0,
			QUADRATIC,
			GAUSSIAN,
			EXPONENTIAL,
			SMOOTHSTEP
		};

	public:
		TerrainTools();

		virtual const char* GetName() override { return "Terrain Tools"; }

		virtual void DrawImGui() override;

		Color GetWireframeColor() { return _wireframeColor; }
		Color GetVertexColor() { return _vertexColor; }
		Color GetBrushColor() { return _brushColor; }

		f32 GetHardness() { return _brushHardness; }
		f32 GetRadius() { return _brushRadius; }
		f32 GetPressure() { return _brushPressure; }

		i32 GetHardnessMode() { return _hardnessMode; }

	private:
		i32 _brushTool = 0;
		i32 _hardnessMode = 0;

		f32 _brushHardness = 1.0f; // Falloff
		f32 _brushRadius = 10.0f;
		f32 _brushPressure = 25.0f;

		Color _wireframeColor = Color::Black;
		Color _vertexColor = Color::Green;
		Color _brushColor = Color::White;
	};
}