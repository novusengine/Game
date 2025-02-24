#pragma once
#include "BaseEditor.h"

namespace Editor
{
    class ItemEditor : public BaseEditor
    {
    public:
        ItemEditor();

        virtual const char* GetName() override { return "Item Editor"; }

        virtual void DrawImGui() override;

    private:

    private:
    };
}