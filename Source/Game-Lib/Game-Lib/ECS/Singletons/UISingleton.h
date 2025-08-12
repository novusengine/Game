#pragma once
#include "Game-Lib/ECS/Components/UI/PanelTemplate.h"
#include "Game-Lib/ECS/Components/UI/TextTemplate.h"

#include <Base/Types.h>
#include <Base/Memory/StackAllocator.h>

#include <robinhood/robinhood.h>
#include <entt/entt.hpp>
#include <map>

namespace ECS::Singletons
{
    struct UISingleton
    {
    public:
        UISingleton() {}

        robin_hood::unordered_map<u32, entt::entity> nameHashToCanvasEntity;

        // Templates
        std::vector <ECS::Components::UI::PanelTemplate> panelTemplates;
        robin_hood::unordered_map<u32, u32> templateHashToPanelTemplateIndex;

        std::vector<ECS::Components::UI::TextTemplate> textTemplates;
        robin_hood::unordered_map<u32, u32> templateHashToTextTemplateIndex;

        // Input handling
        std::map<u64, entt::entity> allHoveredEntities;
        vec2 lastClickPosition = vec2(-1.0f, -1.0f);
        entt::entity clickedEntity = entt::null;
        entt::entity hoveredEntity = entt::null;
        entt::entity focusedEntity = entt::null;
        entt::entity justFocusedEntity = entt::null; // If it was just focused this frame
        entt::entity cursorCanvasEntity = entt::null;

        // Cursor canvas
        Scripting::UI::Widget* cursorCanvas = nullptr;

        // Script widgets, these are actually owned and need to be deleted
        std::vector<Scripting::UI::Widget*> scriptWidgets;
    };
}