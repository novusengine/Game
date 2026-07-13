#pragma once
#include "Game-Lib/ECS/Components/UI/PanelTemplate.h"
#include "Game-Lib/ECS/Components/UI/TextTemplate.h"

#include <Base/Types.h>
#include <Base/Memory/StackAllocator.h>

#include <robinhood/robinhood.h>
#include <entt/entt.hpp>

namespace Scripting::UI
{
    struct Widget;
}

namespace ECS::Singletons
{
    enum class UIInputEventKind : u8
    {
        None,
        Press,
        Release,
        Wheel
    };

    enum class UIInputDebugResult : u8
    {
        Accepted,
        Considered,
        OutsideBounds,
        Clipped,
        Hidden,
        NotInteractable,
        MissingBounds
    };

    struct UIInputCandidate
    {
        entt::entity entity = entt::null;
        vec2 min = vec2(0.0f);
        vec2 max = vec2(0.0f);
        u32 sortKey = 0;
        f32 distanceToMouse = 0.0f;
    };

    struct UIInputDebugRecord
    {
        entt::entity entity = entt::null;
        vec2 min = vec2(0.0f);
        vec2 max = vec2(0.0f);
        UIInputDebugResult result = UIInputDebugResult::Considered;
        u32 sortKey = 0;
        u32 rank = 0;
    };

    struct UIInputDebugSnapshot
    {
        static constexpr u32 MAX_RECORDS = 4096;

        std::vector<UIInputDebugRecord> records;
        UIInputEventKind eventKind = UIInputEventKind::None;
        vec2 mousePosition = vec2(0.0f);
        bool consumedWithoutAcceptedElement = false;
        u32 truncated = 0;
    };

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

        // Active clip sources (widgets owning a clip/mask slot). Few in practice; used by
        // UpdateBoundingRects to re-derive a source's screen-space clip rect when it or an ancestor
        // moves (descendant world rects are no longer propagated). Self-cleaned lazily.
        robin_hood::unordered_set<entt::entity> clipSourceEntities;

        // Input handling
        std::vector<UIInputCandidate> allHoveredEntities;
        UIInputDebugSnapshot inputDebugSnapshot;
        vec2 lastClickPosition = vec2(-1.0f, -1.0f);
        entt::entity clickedEntity = entt::null;
        entt::entity hoveredEntity = entt::null;
        entt::entity focusedEntity = entt::null;
        entt::entity justFocusedEntity = entt::null; // If it was just focused this frame
        entt::entity cursorCanvasEntity = entt::null;

        // Cursor canvas
        Scripting::UI::Widget* cursorCanvas = nullptr;

        i32 sendMessageToChatCallback = -1; // Callback for pushing chat messages into Lua

        // Script widgets, these are actually owned and need to be deleted
        std::vector<Scripting::UI::Widget*> scriptWidgets;
    };
}
