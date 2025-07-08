#pragma once
#include "Game-Lib/UI/Box.h"

#include <Base/Math/Color.h>
#include <Base/Types.h>

#include <Renderer/Descriptors/TextureDesc.h>

#include <entt/fwd.hpp>
#include <optional>

namespace ECS::Components::UI
{
    struct PanelTemplate
    {
    public:
        struct SetFlags
        {
            u8 background : 1 = 0;
            u8 backgroundRT : 1 = 0;
            u8 foreground : 1 = 0;
            u8 color : 1 = 0;
            u8 cornerRadius : 1 = 0;
            u8 texCoords : 1 = 0;
            u8 nineSliceCoords : 1 = 0;
        };
        SetFlags setFlags;

        std::string background;
        Renderer::TextureID backgroundRT;
        entt::entity backgroundRTEntity;
        std::string foreground;
        Color color = Color::White;
        f32 cornerRadius = 0.0f;
        ::UI::Box texCoords;
        ::UI::Box nineSliceCoords;

        std::string onClickTemplate;
        std::string onHoverTemplate;
        std::string onUninteractableTemplate;

        i32 onMouseDownEvent = -1;
        i32 onMouseUpEvent = -1;
        i32 onMouseHeldEvent = -1;

        i32 onHoverBeginEvent = -1;
        i32 onHoverEndEvent = -1;
        i32 onHoverHeldEvent = -1;

        i32 onFocusBeginEvent = -1;
        i32 onFocusEndEvent = -1;
        i32 onFocusHeldEvent = -1;
    };
}