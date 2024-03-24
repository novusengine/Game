#pragma once
#include "BaseEditor.h"

#include <Base/Types.h>

namespace Editor
{
    struct SkyboxSelectorDataBase {};

    class SkyboxSelector : public BaseEditor
    {
    public:
        SkyboxSelector();

        virtual const char* GetName() override { return "Skybox"; }

        virtual void DrawImGui() override;

        void ShowListView();

    private:
        SkyboxSelectorDataBase* _data = nullptr;
    };
}