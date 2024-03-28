#include "TerrainTools.h"

#include "Game/Util/ImguiUtil.h"

namespace Editor
{
    TerrainTools::TerrainTools()
        : BaseEditor(GetName(), true)
    {

    }

    void TerrainTools::OnModeUpdate(bool mode)
    {
        SetIsVisible(mode);
    }

    void TerrainTools::DrawImGui()
    {
        if (ImGui::Begin(GetName()))
        {
            Util::Imgui::GroupHeader("Brush Types (TODO)");
            if (ImGui::BeginTable("Brush Type Table", 2))
            {
                Util::Imgui::ColumnRadioButton("Flatten", &_brushTool, static_cast<i32>(BrushTypes::FLAT));
                Util::Imgui::ColumnRadioButton("Smooth", &_brushTool, static_cast<i32>(BrushTypes::SMOOTH));
                Util::Imgui::ColumnRadioButton("Linear", &_brushTool, static_cast<i32>(BrushTypes::LINEAR));
                Util::Imgui::ColumnRadioButton("Vertex", &_brushTool, static_cast<i32>(BrushTypes::VERTEX));

                ImGui::EndTable();
            }

            Util::Imgui::GroupHeader("Hardness Types");
            if (ImGui::BeginTable("Hardness Type Table", 2))
            {
                Util::Imgui::ColumnRadioButton("Linear", &_hardnessMode, static_cast<i32>(HardnessMode::LINEAR));
                Util::Imgui::ColumnRadioButton("Quadratic", &_hardnessMode, static_cast<i32>(HardnessMode::QUADRATIC));
                Util::Imgui::ColumnRadioButton("Gaussian", &_hardnessMode, static_cast<i32>(HardnessMode::GAUSSIAN));
                Util::Imgui::ColumnRadioButton("Exponential", &_hardnessMode, static_cast<i32>(HardnessMode::EXPONENTIAL));
                Util::Imgui::ColumnRadioButton("Smoothstep", &_hardnessMode, static_cast<i32>(HardnessMode::SMOOTHSTEP));

                ImGui::EndTable();
            }

            ImGui::NewLine();

            Util::Imgui::GroupHeader("Brush Settings");
            Util::Imgui::FloatSlider("Hardness:", &_brushHardness, 0.0f, 1.0f, 0.01f, 0.1f, true);
            Util::Imgui::FloatSlider("Radius:", &_brushRadius, 0.0f, 100.0f, 1.0f, 10.0f, true);
            Util::Imgui::FloatSlider("Pressure:", &_brushPressure, 0.0f, 100.0f, 1.0f, 10.0f, true);

            ImGui::NewLine();

            Util::Imgui::GroupHeader("Chunk Edge Color");
            Util::Imgui::ColorPicker("Chunk Edge Color:", &_chunkEdgeColor, vec2(100.0f, 100.0f));
            ImGui::NewLine();

            Util::Imgui::GroupHeader("Cell Edge Color");
            Util::Imgui::ColorPicker("Cell Edge Color:", &_cellEdgeColor, vec2(100.0f, 100.0f));
            ImGui::NewLine();

            Util::Imgui::GroupHeader("Patch Edge Color");
            Util::Imgui::ColorPicker("Patch Edge Color:", &_patchEdgeColor, vec2(100.0f, 100.0f));
            ImGui::NewLine();

            Util::Imgui::GroupHeader("Vertex Color");
            Util::Imgui::ColorPicker("Vertex Color:", &_vertexColor, vec2(100.0f, 100.0f));
            ImGui::NewLine();

            Util::Imgui::GroupHeader("Brush Color");
            Util::Imgui::ColorPicker("Brush Color:", &_brushColor, vec2(100.0f, 100.0f));
            ImGui::NewLine();
        }

        ImGui::End();
    }
}