#pragma once
#include <Base/Types.h>

#include <entt/fwd.hpp>

namespace ECS::Systems::Editor
{
    // Per-frame editor system for the Lua-driven tools. Runs only meaningful work in
    // developer mode and only when the Lua Inspector has enabled picking/gizmo. Replaces
    // the ImGui Inspector's viewport picking (reusing PixelQuery) and ImGuizmo gizmo
    // (drawn with DebugRenderer), plus the per-selection debug shapes.
    class EditorTools
    {
    public:
        static void Init(entt::registry& registry);
        static void Update(entt::registry& registry, f32 deltaTime);
    };
}
