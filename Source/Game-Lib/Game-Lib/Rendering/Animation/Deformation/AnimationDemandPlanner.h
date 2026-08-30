#pragma once

#include "Game-Lib/Rendering/Camera.h"

#include <FileFormat/Novus/Animation/Animation.h>

#include <cmath>

namespace Animation
{
    struct WorldAnimationBounds
    {
        vec3 center = {};
        vec3 extents = {};
    };

    // Transforms authored animation bounds and tests them before scheduling deformation work.
    // Conservative bounds keep animated models visible when tighter clip data is unavailable.
    inline WorldAnimationBounds TransformAnimationBounds(const FileFormat::Animation::Bounds& bounds, const mat4x4& world)
    {
        WorldAnimationBounds result;
        result.center = vec3(world * vec4(bounds.center, 1.0f));
        const mat3x3 linear(world);
        result.extents = glm::abs(linear[0]) * bounds.extents.x + glm::abs(linear[1]) * bounds.extents.y + glm::abs(linear[2]) * bounds.extents.z;
        return result;
    }

    inline bool IntersectsFrustum(const WorldAnimationBounds& bounds, const Camera& camera)
    {
        for (const vec4& plane : camera.frustum)
        {
            const vec3 normal(plane);
            if (!std::isfinite(plane.w) || glm::dot(normal, normal) < 0.5f)
                continue;
            const f32 radius = glm::dot(glm::abs(normal), bounds.extents);
            if (glm::dot(normal, bounds.center) - plane.w + radius < 0.0f)
                return false;
        }
        return true;
    }
} // namespace Animation
