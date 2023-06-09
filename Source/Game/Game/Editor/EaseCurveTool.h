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
        void ResetTimer();
        void ResetMinMax();

        bool _preview1D = true;
        bool _preview2D = true;

        f32 _timerScale = 1.f;
        f32 _timer = 0.0f;
        bool _inverse = false;

        f32 _currentX = 0.0f;
        f32 _currentY = 0.0f;
        f32 _maxY = 0.0f;
        f32 _minY = 10.0f;

        f32 GetEasePoint(f32 t, i32 n0, i32 n1, i32 polynomialDegree = 2);
    };
}
