#pragma once
#include <Base/Types.h>

#include <entt/fwd.hpp>

struct ImGuiWindow;

namespace Editor
{
    class Viewport;
}

namespace Util
{
    namespace Physics
    {
        bool GetMouseWorldPosition(Editor::Viewport* viewport, vec3& mouseWorldPosition);
        bool GetEntityAtMousePosition(Editor::Viewport* viewport, entt::entity& entity);
    }
}