#pragma once
#include "BaseEditor.h"

namespace Editor
{
    class AnimationController : public BaseEditor
    {
    public:
        AnimationController();

        virtual const char* GetName() override { return "Animation Controller"; }

        virtual void DrawImGui() override;
    };
}