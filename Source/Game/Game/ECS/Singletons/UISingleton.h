#pragma once
#include "Game/UI/Templates.h"

#include <Base/Types.h>
#include <Base/Memory/StackAllocator.h>

#include <robinhood/robinhood.h>
#include <entt/fwd.hpp>

namespace ECS::Singletons
{
    struct UISingleton
    {
    public:
        UISingleton() {}

        robin_hood::unordered_map<u32, entt::entity> nameHashToCanvasEntity;

        // Templates
        std::vector <UI::ButtonTemplate> buttonTemplates;
        robin_hood::unordered_map<u32, u32> templateHashToButtonTemplateIndex;

        std::vector <UI::PanelTemplate> panelTemplates;
        robin_hood::unordered_map<u32, u32> templateHashToPanelTemplateIndex;

        std::vector<UI::TextTemplate> textTemplates;
        robin_hood::unordered_map<u32, u32> templateHashToTextTemplateIndex;
    };
}