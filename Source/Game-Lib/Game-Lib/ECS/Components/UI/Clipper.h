#pragma once
#include <Base/Types.h>

#include <entt/entt.hpp>

namespace ECS::Components::UI
{
    struct Clipper
    {
    public:
        bool clipChildren = false;
        vec2 clipRegionMin = vec2(0.0f, 0.0f);
        vec2 clipRegionMax = vec2(1.0f, 1.0f);
        entt::entity clipRegionOverrideEntity = entt::null;

        bool hasClipMaskTexture = false;
        std::string clipMaskTexture = "";

        // Slot indexes into CanvasRenderer::_widgetClipRects / _widgetMaskInfo.
        // 0 is the sentinel "no clip" / "no mask" entry — reserved on demand
        // when this widget becomes a clip source (clipChildren or hasClipMaskTexture).
        u32 clipRectBufferIndex = 0;
        u32 maskBufferIndex = 0;
    };

    // This clipper itself is dirty
    struct DirtyClipper
    {
    public:
    };

    // This clipper and all its children are dirty
    struct DirtyChildClipper
    {
    public:
    };
}