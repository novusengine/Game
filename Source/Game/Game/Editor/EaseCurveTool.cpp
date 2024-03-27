#include "EaseCurveTool.h"

#include <Base/Math/Interpolation.h>

#include <imgui/imgui.h>

#include <functional>

namespace Editor
{
    using namespace Spline::Interpolation;
    const std::vector<std::function<f32(f32)>> EaseInterpolation[] = {
        {
            Ease::In::Sine,
            Ease::In::Circ,
            Ease::In::Elastic,
            Ease::In::Expo,
            Ease::In::Back,
            Ease::In::Bounce,
            Ease::In::Quadratic,
            Ease::In::Cubic,
            Ease::In::Quartic,
            Ease::In::Quintic,
            Ease::In::Sextic,
            Ease::In::Septic,
            Ease::In::Octic
        },
        {
            Ease::Out::Sine,
            Ease::Out::Circ,
            Ease::Out::Elastic,
            Ease::Out::Expo,
            Ease::Out::Back,
            Ease::Out::Bounce,
            Ease::Out::Quadratic,
            Ease::Out::Cubic,
            Ease::Out::Quartic,
            Ease::Out::Quintic,
            Ease::Out::Sextic,
            Ease::Out::Septic,
            Ease::Out::Octic
        },
        {
            Ease::InOut::Sine,
            Ease::InOut::Circ,
            Ease::InOut::Elastic,
            Ease::InOut::Expo,
            Ease::InOut::Back,
            Ease::InOut::Bounce,
            Ease::InOut::Quadratic,
            Ease::InOut::Cubic,
            Ease::InOut::Quartic,
            Ease::InOut::Quintic,
            Ease::InOut::Sextic,
            Ease::InOut::Septic,
            Ease::InOut::Octic
        }
    };

    EaseCurveTool::EaseCurveTool() : BaseEditor(GetName(), false)
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

            DrawHeader();
            Draw2DPreview();
            Draw1DPreview();
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

    void EaseCurveTool::OnModeUpdate(bool mode)
    {
        SetIsVisible(mode);
    }

    void EaseCurveTool::DrawHeader()
    {
        ImGui::PushItemWidth(80);
        if (ImGui::BeginCombo("##EaseComboBoxType", _easeType[_selectedEaseType]))
        {
            for (i32 i = 0; i < _easeTypeCount; i++)
            {
                bool isSelected = (_selectedEaseType == i);
                if (ImGui::Selectable(_easeType[i], isSelected))
                {
                    _selectedEaseType = i;
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
        if (ImGui::BeginCombo("##EaseComboBoxName", _easeName[_selectedEaseName]))
        {
            for (i32 i = 0; i < _easeNameCount; i++)
            {
                bool isSelected = (_selectedEaseName == i);
                if (ImGui::Selectable(_easeName[i], isSelected))
                {
                    _selectedEaseName = i;
                    ResetTimer();
                    ResetMinMax();
                }

                if (isSelected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::PopItemWidth();

        if (_selectedEaseName == 13)
        {
            ImGui::SeparatorText("Parameters");
            ImGui::PushItemWidth(100);
            ImGui::DragFloat("Polynomial Degree", &_polynomialDegree, 1.0f, 2.0f, 32.0f);
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
        if (ImGui::DragFloat("Canvas Scale", &_canvasScale, 0.01f, 0.5f, 0.8f))
            _invHalfCanvasScale = (1.f - _canvasScale) / 2.f;
        ImGui::PopItemWidth();

        ImGui::PushItemWidth(100);
        ImGui::DragInt("Number of segments", &_segmentCount, 1.f, 5, 50);
        ImGui::PopItemWidth();

        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("This is only for curve preview");
        }

        ImGui::Separator();
    }

    void EaseCurveTool::Draw2DPreview()
    {
        if (!_preview2D)
            return;

        ImVec2 size(320.f, 320.f);
        ImGui::InvisibleButton("EaseCanvas", size);
        ImVec2 p0 = ImGui::GetItemRectMin();
        ImVec2 p1 = ImGui::GetItemRectMax();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->PushClipRect(p0, p1, true);

        ImVec2 VLTop = {p0.x + (size.x * _invHalfCanvasScale), p0.y};
        ImVec2 VLBottom = {VLTop.x, p1.y};
        ImVec2 VRTop = {p1.x - (size.x * _invHalfCanvasScale), p0.y};
        ImVec2 VRBottom = {VRTop.x, p1.y};
        ImVec2 HBLeft = {p0.x, p1.y - (size.y * _invHalfCanvasScale)};
        ImVec2 HBRight = {p1.x, HBLeft.y};
        ImVec2 HTLeft = {p0.x, p0.y + (size.y * _invHalfCanvasScale)};
        ImVec2 HTRight = {p1.x, HTLeft.y};

        drawList->AddLine(VLTop, VLBottom, _whiteHalfColor);
        drawList->AddLine(VRTop, VRBottom, _whiteHalfColor);
        drawList->AddLine(HBLeft, HBRight, _whiteHalfColor);
        drawList->AddLine(HTLeft, HTRight, _whiteHalfColor);

        const f32 textOff = 5.0f;
        drawList->AddText(ImVec2(VLBottom.x - textOff - ImGui::CalcTextSize("0").x, HBLeft.y + textOff), _whiteFullColor, "0");
        drawList->AddText(ImVec2(VRBottom.x + textOff, HBRight.y + textOff), _whiteFullColor, "t");
        drawList->AddText(ImVec2(VLBottom.x - textOff - ImGui::CalcTextSize("f(t)").x, HTLeft.y - textOff - ImGui::CalcTextSize("f(t)").y), _whiteFullColor, "f(t)");
        drawList->AddText(ImVec2(VRBottom.x + textOff, HTRight.y - textOff - ImGui::CalcTextSize("1").y), _whiteFullColor, "1");

        std::vector<f32> easePoints;
        easePoints.reserve(_segmentCount + 1);
        for (i32 i = 0; i <= _segmentCount; i++)
            easePoints.emplace_back(GetEasePoint(f32(i) / f32(_segmentCount), _selectedEaseType, _selectedEaseName, _polynomialDegree));

        for (i32 i = 0; i < easePoints.size() - 1; i++)
        {
            f32 pt0 = easePoints[i];
            f32 pt1 = easePoints[i + 1];

            f32 x0 = f32(i) * ((size.x * _canvasScale) / f32(_segmentCount)) + p0.x + (size.x * _invHalfCanvasScale);
            f32 x1 = (f32)(i + 1) * ((size.x * _canvasScale) / f32(_segmentCount)) + p0.x + (size.x * _invHalfCanvasScale);

            f32 y0 = (p0.y + size.y - (size.y * _invHalfCanvasScale)) - pt0 * (size.y * _canvasScale);
            f32 y1 = (p0.y + size.y - (size.y * _invHalfCanvasScale)) - pt1 * (size.y * _canvasScale);

            drawList->AddLine(ImVec2(x0, y0), ImVec2(x1, y1), _redColor);
        }

        _currentX = _timer / _timerScale;
        _currentY = GetEasePoint(_currentX, _selectedEaseType, _selectedEaseName, _polynomialDegree);

        _minY = std::min(_minY, _currentY);
        _maxY = std::max(_maxY, _currentY);

        f32 xCircle = p0.x + (size.x * _invHalfCanvasScale) + (_currentX * size.x * _canvasScale);
        f32 yCircle = (p1.y - (size.y * _invHalfCanvasScale)) - _currentY * (size.y * _canvasScale);

        drawList->AddRect(p0, p1, _whiteFullColor);
        drawList->AddCircleFilled(ImVec2(xCircle, yCircle), 4.f, _whiteHalfColor);
        drawList->PopClipRect();

        ImGui::Text("t = %.3f", _currentX);
        ImGui::Text("f(t) = %.3f", _currentY);
        ImGui::Text("min = %.3f - max = %.3f)", _minY, _maxY);
    }

    void EaseCurveTool::Draw1DPreview()
    {
        if (!_preview1D)
            return;

        ImGui::SeparatorText("1D Preview");
        ImGui::Checkbox("Bouncing Preview", &_useBouncing);

        // linear preview
        ImGui::Text("Linear");
        {
            ImVec2 size(320.f, 48.f);
            ImGui::InvisibleButton("EaseCanvas", size);
            ImVec2 p0 = ImGui::GetItemRectMin();
            ImVec2 p1 = ImGui::GetItemRectMax();
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->PushClipRect(p0, p1, true);

            ImVec2 VLTop = { p0.x + (size.x * _invHalfCanvasScale), p0.y };
            ImVec2 VLBottom = { VLTop.x, p1.y };
            ImVec2 VRTop = { p1.x - (size.x * _invHalfCanvasScale), p0.y };
            ImVec2 VRBottom = { VRTop.x, p1.y };
            ImVec2 HLeft = { p0.x, p1.y - (size.y * 0.5f) };
            ImVec2 HRight = { p1.x, HLeft.y };

            drawList->AddLine(VLTop, VLBottom, _whiteHalfColor);
            drawList->AddLine(VRTop, VRBottom, _whiteHalfColor);
            drawList->AddLine(HLeft, HRight, _whiteHalfColor);

            f32 start = p0.x + (size.x * _invHalfCanvasScale);
            f32 end = p0.x + (size.x * (1.f - _invHalfCanvasScale));
            if (_useBouncing && _inverse)
            {
                start = end;
                end = p0.x + (size.x * _invHalfCanvasScale);
            }

            f32 x = Spline::Interpolation::Linear::Lerp(_timer / _timerScale, start, end);
            f32 y = p0.y + (size.y / 2.0f);

            drawList->AddCircleFilled(ImVec2(x, y), 8.f, _redColor);
            drawList->AddRect(p0, p1, _whiteFullColor);
            drawList->PopClipRect();
        }

        // ease preview
        {
            ImGui::Text("%s", _easeName[_selectedEaseName]);
            ImVec2 size(320.f, 48.f);
            ImGui::InvisibleButton("EaseCanvas", size);
            ImVec2 p0 = ImGui::GetItemRectMin();
            ImVec2 p1 = ImGui::GetItemRectMax();
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->PushClipRect(p0, p1, true);

            ImVec2 VLTop = { p0.x + (size.x * _invHalfCanvasScale), p0.y };
            ImVec2 VLBottom = { VLTop.x, p1.y };
            ImVec2 VRTop = { p1.x - (size.x * _invHalfCanvasScale), p0.y };
            ImVec2 VRBottom = { VRTop.x, p1.y };
            ImVec2 HLeft = { p0.x, p1.y - (size.y * 0.5f) };
            ImVec2 HRight = { p1.x, HLeft.y };

            drawList->AddLine(VLTop, VLBottom, _whiteHalfColor);
            drawList->AddLine(VRTop, VRBottom, _whiteHalfColor);
            drawList->AddLine(HLeft, HRight, _whiteHalfColor);

            f32 x = GetEasePoint(_timer / _timerScale, _selectedEaseType, _selectedEaseName, _polynomialDegree);

            f32 xOffset = x * (size.x * _canvasScale);
            f32 x0 = p0.x + (size.x * _invHalfCanvasScale) + xOffset;
            if (_useBouncing && _inverse)
                x0 = p0.x + (size.x * (1.f - _invHalfCanvasScale)) - xOffset;

            f32 y0 = p0.y + (size.y / 2.0f);

            drawList->AddCircleFilled(ImVec2(x0, y0), 8.f, _redColor);

            drawList->AddRect(p0, p1, _whiteFullColor);
            drawList->PopClipRect();
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

    f32 EaseCurveTool::GetEasePoint(f32 t, i32 interpolationType, i32 interpolationName, f32 polynomialDegree)
    {
        f32 result = 0.0f;

        if (interpolationType >= 0 && interpolationType < _easeTypeCount)
        {
            if (interpolationName < 13)
            {
                result = EaseInterpolation[interpolationType][interpolationName](t);
            }
            else if (interpolationName == 13)
            {
                switch (interpolationType)
                {
                    case 0:
                        result = Spline::Interpolation::Ease::In::Polynomial(t, polynomialDegree);
                        break;
                    case 1:
                        result = Spline::Interpolation::Ease::Out::Polynomial(t, polynomialDegree);
                        break;
                    case 2:
                        result = Spline::Interpolation::Ease::InOut::Polynomial(t, polynomialDegree);
                        break;
                    default:
                        break;
                }
            }
        }

        return result;
    }
}