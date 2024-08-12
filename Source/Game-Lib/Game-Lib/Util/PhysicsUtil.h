#pragma once
#include <Base/Types.h>

#include <entt/fwd.hpp>

struct ImGuiWindow;

namespace JPH
{
    class RayCastResult;
    class PhysicsSystem;
}

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
        bool CastRay(JPH::PhysicsSystem& physicsSystem, vec3& start, vec3& direction, JPH::RayCastResult& result);
    }
}