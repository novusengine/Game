#include "EaseCurveTool.h"

#include <Base/Math/Interpolation.h>

#include <imgui/imgui.h>

namespace Editor
{
    EaseCurveTool::EaseCurveTool()
        : BaseEditor(GetName(), false)
    {

    }

    void EaseCurveTool::DrawImGui()
    {
        if (ImGui::Begin(GetName(), NULL, ImGuiWindowFlags_AlwaysAutoResize))
        {
            if (OpenMenu("Settings"))
            {
                ImGui::Checkbox("2D Preview", &_preview2D);
                ImGui::Checkbox("1D Preview", &_preview1D);

                CloseMenu();
            }

            static bool useInverse = true;

            const char* easeType[] = {"In", "Out", "InOut"};
            static i32 selectedEaseType = 0;

            const char* easeName[] = {"Sine", "Circ", "Elastic", "Expo", "Back", "Bounce",
                "Quadratic", "Cubic", "Quartic", "Quintic", "Sextic", "Septic", "Octic", "Polynomial"};
            static i32 selectedEaseName = 0;

            static i32 segment = 30;

            static f32 canvasScale = 0.6f;
            static f32 invHalfCanvasScale = 0.2f;

            static i32 polynomialDegree = 2;

            ImGui::PushItemWidth(80);
            if (ImGui::BeginCombo("##EaseComboBoxType", easeType[selectedEaseType]))
            {
                for (i32 i = 0; i < 3; i++)
                {
                    bool isSelected = (selectedEaseType == i);
                    if (ImGui::Selectable(easeType[i], isSelected))
                    {
                        selectedEaseType = i;
                        ResetTimer();
                        ResetMinMax();
                    }

                    if (isSelected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::PopItemWidth();
            ImGui::SameLine();
            ImGui::Text("::");
            ImGui::SameLine();
            ImGui::PushItemWidth(160);
            if (ImGui::BeginCombo("##EaseComboBoxName", easeName[selectedEaseName]))
            {
                for (i32 i = 0; i < 14; i++)
                {
                    bool isSelected = (selectedEaseName == i);
                    if (ImGui::Selectable(easeName[i], isSelected))
                    {
                        selectedEaseName = i;
                        ResetTimer();
                        ResetMinMax();
                    }

                    if (isSelected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::PopItemWidth();

            if (selectedEaseName == 13)
            {
                ImGui::SeparatorText("Parameters");
                ImGui::PushItemWidth(100);
                ImGui::DragInt("Polynomial Degree", &polynomialDegree, 1, 2, 32);
                ImGui::PopItemWidth();
            }

            ImGui::SeparatorText("Animation Timer");
            ImGui::PushItemWidth(160);
            if (ImGui::DragFloat("seconds##EaseTimer", &_timerScale, 0.001f, 1.f, 5.f, "%.3f", ImGuiSliderFlags_ClampOnInput))
            {
                ResetTimer();
                _inverse = false;
            }
            ImGui::PopItemWidth();

            ImGui::SeparatorText("2D Preview");

            ImGui::PushItemWidth(100);
            if (ImGui::DragFloat("Canvas Scale", &canvasScale, 0.01f, 0.5f, 0.8f))
                invHalfCanvasScale = (1.f - canvasScale) / 2.f;
            ImGui::PopItemWidth();

            ImGui::PushItemWidth(100);
            ImGui::DragInt("Number of segments", &segment, 1.f, 5, 50);
            ImGui::PopItemWidth();

            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("This is only for curve preview");
            }

            ImGui::Separator();

            if (_preview2D)
            {
                ImVec2 size(320.f, 320.f);
                ImGui::InvisibleButton("EaseCanvas", size);
                ImVec2 p0 = ImGui::GetItemRectMin();
                ImVec2 p1 = ImGui::GetItemRectMax();
                ImDrawList* drawList = ImGui::GetWindowDrawList();
                drawList->PushClipRect(p0, p1);

                ImVec2 VLTop = {p0.x + (size.x * invHalfCanvasScale), p0.y};
                ImVec2 VLBottom = {VLTop.x, p1.y};
                ImVec2 VRTop = {p1.x - (size.x * invHalfCanvasScale), p0.y};
                ImVec2 VRBottom = {VRTop.x, p1.y};
                ImVec2 HBLeft = {p0.x, p1.y - (size.y * invHalfCanvasScale)};
                ImVec2 HBRight = {p1.x, HBLeft.y};
                ImVec2 HTLeft = {p0.x, p0.y + (size.y * invHalfCanvasScale)};
                ImVec2 HTRight = {p1.x, HTLeft.y};

                drawList->AddLine(VLTop, VLBottom, IM_COL32(255, 255, 255, 100));
                drawList->AddLine(VRTop, VRBottom, IM_COL32(255, 255, 255, 100));
                drawList->AddLine(HBLeft, HBRight, IM_COL32(255, 255, 255, 100));
                drawList->AddLine(HTLeft, HTRight, IM_COL32(255, 255, 255, 100));

                const f32 textOff = 5.0f;
                drawList->AddText(ImVec2(VLBottom.x - textOff - ImGui::CalcTextSize("0").x, HBLeft.y + textOff), IM_COL32(255, 255, 255, 255), "0");
                drawList->AddText(ImVec2(VRBottom.x + textOff, HBRight.y + textOff), IM_COL32(255, 255, 255, 255), "t");
                drawList->AddText(ImVec2(VLBottom.x - textOff - ImGui::CalcTextSize("f(t)").x, HTLeft.y - textOff - ImGui::CalcTextSize("f(t)").y), IM_COL32(255, 255, 255, 255), "f(t)");
                drawList->AddText(ImVec2(VRBottom.x + textOff, HTRight.y - textOff - ImGui::CalcTextSize("1").y), IM_COL32(255, 255, 255, 255), "1");

                std::vector<f32> easePoints;
                easePoints.reserve(segment + 1);
                for (i32 i = 0; i <= segment; i++)
                    easePoints.emplace_back(GetEasePoint(f32(i) / f32(segment), selectedEaseType, selectedEaseName, polynomialDegree));

                for (i32 i = 0; i < easePoints.size() - 1; i++)
                {
                    f32 pt0 = easePoints[i];
                    f32 pt1 = easePoints[i + 1];

                    f32 x0 = f32(i) * ((size.x * canvasScale) / f32(segment)) + p0.x + (size.x * invHalfCanvasScale);
                    f32 x1 = (f32)(i + 1) * ((size.x * canvasScale) / f32(segment)) + p0.x + (size.x * invHalfCanvasScale);

                    f32 y0 = (p0.y + size.y - (size.y * invHalfCanvasScale)) - pt0 * (size.y * canvasScale);
                    f32 y1 = (p0.y + size.y - (size.y * invHalfCanvasScale)) - pt1 * (size.y * canvasScale);

                    drawList->AddLine(ImVec2(x0, y0), ImVec2(x1, y1), IM_COL32(255, 0, 0, 255));
                }

                _currentX = _timer / _timerScale;
                _currentY = GetEasePoint(_currentX, selectedEaseType, selectedEaseName, polynomialDegree);

                _minY = std::min(_minY, _currentY);
                _maxY = std::max(_maxY, _currentY);

                f32 xCircle = p0.x + (size.x * invHalfCanvasScale) + (_currentX * size.x * canvasScale);
                f32 yCircle = (p1.y - (size.y * invHalfCanvasScale)) - _currentY * (size.y * canvasScale);

                drawList->AddRect(p0, p1, IM_COL32(255, 255, 255, 255));
                drawList->AddCircleFilled(ImVec2(xCircle, yCircle), 4.f, IM_COL32(255, 255, 255, 100));
                drawList->PopClipRect();

                ImGui::Text("t = %.3f", _currentX);
                ImGui::Text("f(t) = %.3f", _currentY);
                ImGui::Text("min = %.3f - max = %.3f)", _minY, _maxY);
            }

            if (_preview1D)
            {
                ImGui::SeparatorText("1D Preview");
                ImGui::Checkbox("Bouncing Preview", &useInverse);

                // linear preview
                ImGui::Text("Linear");
                {
                    ImVec2 size(320.f, 48.f);
                    ImGui::InvisibleButton("EaseCanvas", size);
                    ImVec2 p0 = ImGui::GetItemRectMin();
                    ImVec2 p1 = ImGui::GetItemRectMax();
                    ImDrawList* drawList = ImGui::GetWindowDrawList();
                    drawList->PushClipRect(p0, p1);

                    ImVec2 VLTop = { p0.x + (size.x * invHalfCanvasScale), p0.y };
                    ImVec2 VLBottom = { VLTop.x, p1.y };
                    ImVec2 VRTop = { p1.x - (size.x * invHalfCanvasScale), p0.y };
                    ImVec2 VRBottom = { VRTop.x, p1.y };
                    ImVec2 HLeft = { p0.x, p1.y - (size.y * 0.5f) };
                    ImVec2 HRight = { p1.x, HLeft.y };

                    drawList->AddLine(VLTop, VLBottom, IM_COL32(255,255,255,100));
                    drawList->AddLine(VRTop, VRBottom, IM_COL32(255,255,255,100));
                    drawList->AddLine(HLeft, HRight, IM_COL32(255,255,255,100));

                    f32 start = p0.x + (size.x * invHalfCanvasScale);
                    f32 end = p0.x + (size.x * (1.f - invHalfCanvasScale));
                    if (useInverse && _inverse)
                    {
                        start = end;
                        end = p0.x + (size.x * invHalfCanvasScale);
                    }

                    f32 x = Spline::Interpolation::Linear::Lerp(_timer / _timerScale, start, end);
                    f32 y = p0.y + (size.y / 2.0f);

                    drawList->AddCircleFilled(ImVec2(x, y), 8.f, IM_COL32(255,0,0,255));
                    drawList->AddRect(p0, p1, IM_COL32(255,255,255,255));
                    drawList->PopClipRect();
                }

                // ease preview
                {
                    ImGui::Text("%s", easeName[selectedEaseName]);
                    ImVec2 size(320.f, 48.f);
                    ImGui::InvisibleButton("EaseCanvas", size);
                    ImVec2 p0 = ImGui::GetItemRectMin();
                    ImVec2 p1 = ImGui::GetItemRectMax();
                    ImDrawList* drawList = ImGui::GetWindowDrawList();
                    drawList->PushClipRect(p0, p1);

                    ImVec2 VLTop = { p0.x + (size.x * invHalfCanvasScale), p0.y };
                    ImVec2 VLBottom = { VLTop.x, p1.y };
                    ImVec2 VRTop = { p1.x - (size.x * invHalfCanvasScale), p0.y };
                    ImVec2 VRBottom = { VRTop.x, p1.y };
                    ImVec2 HLeft = { p0.x, p1.y - (size.y * 0.5f) };
                    ImVec2 HRight = { p1.x, HLeft.y };

                    drawList->AddLine(VLTop, VLBottom, IM_COL32(255,255,255,100));
                    drawList->AddLine(VRTop, VRBottom, IM_COL32(255,255,255,100));
                    drawList->AddLine(HLeft, HRight, IM_COL32(255,255,255,100));

                    f32 x = GetEasePoint(_timer / _timerScale, selectedEaseType, selectedEaseName, polynomialDegree);

                    f32 xOffset = x * (size.x * canvasScale);
                    f32 x0 = p0.x + (size.x * invHalfCanvasScale) + xOffset;
                    if (useInverse && _inverse)
                        x0 = p0.x + (size.x * (1.f - invHalfCanvasScale)) - xOffset;

                    f32 y0 = p0.y + (size.y / 2.0f);

                    drawList->AddCircleFilled(ImVec2(x0, y0), 8.f, IM_COL32(255,0,0,255));


                    drawList->AddRect(p0, p1, IM_COL32(255,255,255,255));
                    drawList->PopClipRect();
                }
            }
        }
        ImGui::End();
    }

    void EaseCurveTool::Update(f32 deltaTime)
    {
        _timer += deltaTime;
        if (_timer > _timerScale)
        {
            ResetTimer();
            _inverse = !_inverse;
        }
    }

    void EaseCurveTool::ResetTimer()
    {
        _timer = 0.0f;
        _currentX = 0.0f;
        _currentY = 0.0f;
    }

    void EaseCurveTool::ResetMinMax()
    {
        _minY = 10.0f;
        _maxY = 0.0f;
    }

    f32 EaseCurveTool::GetEasePoint(f32 t, i32 n0, i32 n1, i32 polynomialDegree)
    {
        switch (n0)
        {
        case 0:
            switch(n1)
            {
            case 0:
                return Spline::Interpolation::Ease::In::Sine(t);
            case 1:
                return Spline::Interpolation::Ease::In::Circ(t);
            case 2:
                return Spline::Interpolation::Ease::In::Elastic(t);
            case 3:
                return Spline::Interpolation::Ease::In::Expo(t);
            case 4:
                return Spline::Interpolation::Ease::In::Back(t);
            case 5:
                return Spline::Interpolation::Ease::In::Bounce(t);
            case 6:
                return Spline::Interpolation::Ease::In::Quadratic(t);
            case 7:
                return Spline::Interpolation::Ease::In::Cubic(t);
            case 8:
                return Spline::Interpolation::Ease::In::Quartic(t);
            case 9:
                return Spline::Interpolation::Ease::In::Quintic(t);
            case 10:
                return Spline::Interpolation::Ease::In::Sextic(t);
            case 11:
                return Spline::Interpolation::Ease::In::Septic(t);
            case 12:
                return Spline::Interpolation::Ease::In::Octic(t);
            case 13:
                return Spline::Interpolation::Ease::In::Polynomial(t, polynomialDegree);
            default:
                return 0.f;
            }
        case 1:
            switch(n1)
            {
            case 0:
                return Spline::Interpolation::Ease::Out::Sine(t);
            case 1:
                return Spline::Interpolation::Ease::Out::Circ(t);
            case 2:
                return Spline::Interpolation::Ease::Out::Elastic(t);
            case 3:
                return Spline::Interpolation::Ease::Out::Expo(t);
            case 4:
                return Spline::Interpolation::Ease::Out::Back(t);
            case 5:
                return Spline::Interpolation::Ease::Out::Bounce(t);
            case 6:
                return Spline::Interpolation::Ease::Out::Quadratic(t);
            case 7:
                return Spline::Interpolation::Ease::Out::Cubic(t);
            case 8:
                return Spline::Interpolation::Ease::Out::Quartic(t);
            case 9:
                return Spline::Interpolation::Ease::Out::Quintic(t);
            case 10:
                return Spline::Interpolation::Ease::Out::Sextic(t);
            case 11:
                return Spline::Interpolation::Ease::Out::Septic(t);
            case 12:
                return Spline::Interpolation::Ease::Out::Octic(t);
            case 13:
                return Spline::Interpolation::Ease::Out::Polynomial(t, polynomialDegree);
            default:
                return 0.f;
            }
        case 2:
            switch(n1)
            {
            case 0:
                return Spline::Interpolation::Ease::InOut::Sine(t);
            case 1:
                return Spline::Interpolation::Ease::InOut::Circ(t);
            case 2:
                return Spline::Interpolation::Ease::InOut::Elastic(t);
            case 3:
                return Spline::Interpolation::Ease::InOut::Expo(t);
            case 4:
                return Spline::Interpolation::Ease::InOut::Back(t);
            case 5:
                return Spline::Interpolation::Ease::InOut::Bounce(t);
            case 6:
                return Spline::Interpolation::Ease::InOut::Quadratic(t);
            case 7:
                return Spline::Interpolation::Ease::InOut::Cubic(t);
            case 8:
                return Spline::Interpolation::Ease::InOut::Quartic(t);
            case 9:
                return Spline::Interpolation::Ease::InOut::Quintic(t);
            case 10:
                return Spline::Interpolation::Ease::InOut::Sextic(t);
            case 11:
                return Spline::Interpolation::Ease::InOut::Septic(t);
            case 12:
                return Spline::Interpolation::Ease::InOut::Octic(t);
            case 13:
                return Spline::Interpolation::Ease::InOut::Polynomial(t, polynomialDegree);
            default:
                return 0.f;
            }
        default:
            return 0.f;
        }
    }
}