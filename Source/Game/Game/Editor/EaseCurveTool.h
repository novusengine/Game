#pragma once
#include "BaseEditor.h"

namespace Editor
{
    class EaseCurveTool : public BaseEditor
    {
    public:
        EaseCurveTool();

        virtual const char* GetName() override { return "EaseCurve Tool"; }

        virtual void DrawImGui() override;

        virtual void Update(f32 deltaTime) override;

    private:
        f32 GetEasePoint(f32 t, i32 interpolationType, i32 interpolationName, f32 polynomialDegree = 2.0f);

        void ResetTimer();
        void ResetMinMax();

        void DrawHeader();
        void Draw2DPreview();
        void Draw1DPreview();

    private:
        const ImU32 _whiteFullColor = IM_COL32(255, 255, 255, 255);
        const ImU32 _whiteHalfColor = IM_COL32(255, 255, 255, 100);
        const ImU32 _redColor = IM_COL32(255, 0, 0, 255);

        bool _preview1D = true;
        bool _preview2D = true;

        f32 _timerScale = 1.f;
        f32 _timer = 0.0f;
        bool _inverse = false;

        f32 _currentX = 0.0f;
        f32 _currentY = 0.0f;
        f32 _maxY = 0.0f;
        f32 _minY = 10.0f;

        bool _useBouncing = true;
        i32 _segmentCount = 30;
        f32 _canvasScale = 0.6f;
        f32 _invHalfCanvasScale = 0.2f;
        f32 _polynomialDegree = 2;

        static const u8 _easeTypeCount = 3;
        const char* const _easeType[_easeTypeCount] = {"In", "Out", "InOut"};
        i32 _selectedEaseType = 0;

        static const u8 _easeNameCount = 14;
        const char* const _easeName[_easeNameCount] = {"Sine", "Circ", "Elastic", "Expo", "Back", "Bounce",
            "Quadratic", "Cubic", "Quartic", "Quintic", "Sextic", "Septic", "Octic", "Polynomial"};
        i32 _selectedEaseName = 0;
    };
}
