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
				Util::Imgui::ColumnRadioButton("Flatten", &_brushTool, BrushTypes::FLAT);
				Util::Imgui::ColumnRadioButton("Smooth", &_brushTool, BrushTypes::SMOOTH);
				Util::Imgui::ColumnRadioButton("Linear", &_brushTool, BrushTypes::LINEAR);
				Util::Imgui::ColumnRadioButton("Vertex", &_brushTool, BrushTypes::VERTEX);

				ImGui::EndTable();
			}

			ImGui::NewLine();

			Util::Imgui::GroupHeader("Brush Settings");

			Util::Imgui::FloatSlider("Hardness:", &_brushHardness, 0.0f, 100.0f, 0.01f, 0.1f, true);
			Util::Imgui::FloatSlider("Radius:", &_brushRadius, 0.0f, 100.0f, 0.01f, 0.1f, true);
			Util::Imgui::FloatSlider("Pressure:", &_brushPressure, 0.0f, 100.0f, 0.01f, 0.1f, true);

			ImGui::NewLine();

			Util::Imgui::GroupHeader("Vertex Color Picker");

			Util::Imgui::ColorPicker("Vertex Color:", &_vertexColor, vec2(100.0f, 100.0f));
		}

		ImGui::End();
	}
}