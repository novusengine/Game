#pragma once
#include "BaseEditor.h"

namespace Editor
{
    class SpellEditor : public BaseEditor
    {
    public:
        SpellEditor();

        virtual const char* GetName() override { return "Spell Editor"; }

        virtual void DrawImGui() override;

    private:

    private:
    };
}