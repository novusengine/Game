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
        bool CastRay(JPH::PhysicsSystem& physicsSystem, vec3& start, vec3& direction, JPH::RayCastResult& result);

        // Casts a world ray (start + dir, dir gets extended internally) against all physics geometry,
        // optionally ignoring one body. Pass std::numeric_limits<u32>::max() as ignoreBodyID for none.
        // Returns the hit world position in outHitPos. Fetches the JoltState itself.
        bool CastRayWorld(const vec3& start, const vec3& dir, u32 ignoreBodyID, vec3& outHitPos);
    }
}