#pragma once
#include <Base/Types.h>

#include <entt/fwd.hpp>
#include <entt/entt.hpp>

#include <string>

namespace ECS::Singletons
{
    enum class GizmoOperation : u8
    {
        Translate = 0,
        Rotate = 1,
        Scale = 2,
    };

    enum class GizmoMode : u8
    {
        Local = 0,
        World = 1,
    };

    // Shared state for the Lua-driven editor tools (Hierarchy/Inspector) and the C++
    // editor systems (picking, gizmo, debug draw). The Lua side reads/writes this through
    // the dev-only "Editor" handler; the C++ systems consume it each frame.
    struct EditorSelection
    {
    public:
        entt::entity selectedEntity = entt::null;

        // Debug shapes drawn for the selected entity. Kept here rather than in CVars
        // because they are per-selection display state, not global render config.
        bool drawOBB = false;
        bool drawWorldAABB = false;

        // Picking is only run while the Lua Inspector wants it (set on Show/Hide).
        bool pickingEnabled = false;

        // Gizmo is only run while the Lua Inspector wants it (set on Show/Hide).
        bool gizmoEnabled = false;
        GizmoOperation gizmoOperation = GizmoOperation::Translate;
        GizmoMode gizmoMode = GizmoMode::World;

        // Drag-to-spawn from the Asset Browser. Lua sets the request (model path) on mouse-down;
        // the editor system consumes it and runs the cursor-follow spawn (see EditorTools).
        bool dragSpawnRequested = false;
        std::string dragSpawnModelPath;
    };
}
