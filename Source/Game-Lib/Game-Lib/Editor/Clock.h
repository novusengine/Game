#pragma once
#include "BaseEditor.h"

namespace Editor
{
    class Clock : public BaseEditor
    {
    public:
        Clock();

        virtual const char* GetName() override { return "Clock"; }

        virtual void DrawImGui() override;
    };
}