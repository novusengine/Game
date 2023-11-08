#include "TerrainTools.h"

#include "Game/Util/ImguiUtil.h"

namespace Editor
{
	TerrainTools::TerrainTools()
		: BaseEditor(GetName(), true)
	{

	}

	void TerrainTools::DrawImGui()
	{
		if (ImGui::Begin(GetName()))
		{
			Util::Imgui::GroupHeader("Brush Types");

			if (ImGui::BeginTable("Brush Type Table", 2))
			{
				Util::Imgui::ColumnCheckBox("Flatten", _brushTypes, BrushTypes::FLAT, BrushTypes::COUNT);
				Util::Imgui::ColumnCheckBox("Smooth", _brushTypes, BrushTypes::SMOOTH, BrushTypes::COUNT);
				Util::Imgui::ColumnCheckBox("Linear", _brushTypes, BrushTypes::LINEAR, BrushTypes::COUNT);
				Util::Imgui::ColumnCheckBox("Vertex", _brushTypes, BrushTypes::VERTEX, BrushTypes::COUNT);

				ImGui::EndTable();
			}

			ImGui::NewLine();

			Util::Imgui::GroupHeader("Brush Settings");

			Util::Imgui::FloatSlider("Hardness:", &_brushHardness, 0.0f, 100.0f, 0.01f, 0.1f, true);
			Util::Imgui::FloatSlider("Radius:", &_brushRadius, 0.0f, 100.0f, 0.01f, 0.1f, true);
			Util::Imgui::FloatSlider("Pressure:", &_brushPressure, 0.0f, 100.0f, 0.01f, 0.1f, true);

			ImGui::NewLine();

			Util::Imgui::GroupHeader("Vertex Color Picker");

			Util::Imgui::ColorPicker("Vertex Color:", &_vertexColor, ImVec2(100.0f, 100.0f));
		}
		ImGui::End();
	}
}