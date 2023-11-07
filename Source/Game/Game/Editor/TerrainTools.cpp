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
			Util::Imgui::ColorPicker("Vertex Color:", &_vertexColor);

			Util::Imgui::FloatSlider("Hardness:", &_brushHardness, 0.0f, 100.0f, 0.01f, 0.1f, true);
			Util::Imgui::FloatSlider("Radius:", &_brushRadius, 0.0f, 100.0f, 0.01f, 0.1f, true);
			Util::Imgui::FloatSlider("Pressure:", &_brushPressure, 0.0f, 100.0f, 0.01f, 0.1f, true);
		}

		ImGui::End();
	}
}