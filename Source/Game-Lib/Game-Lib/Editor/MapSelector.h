#pragma once
#include "BaseEditor.h"

#include <Base/Types.h>

namespace Editor
{
    class MapSelector : public BaseEditor
    {
    public:
        MapSelector();

        virtual const char* GetName() override { return "Map"; }

        virtual void DrawImGui() override;

        void ShowListViewWithIcons();

    private:
        void* _mapIcons[5] = { nullptr };
        vec2 _mapIconSizes[5];

        u32 _currentSelectedMapID = 0;
    };
}