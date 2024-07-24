#pragma once
#include "BaseEditor.h"

namespace Editor
{
    class NetworkedInfo : public BaseEditor
    {
    public:
        NetworkedInfo();

        virtual const char* GetName() override { return "Networked Info"; }

        virtual void DrawImGui() override;

    private:

    private:
    };
}