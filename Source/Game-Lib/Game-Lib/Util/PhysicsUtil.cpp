#include "PhysicsUtil.h"

#include "Game-Lib/ECS/Components/Camera.h"
#include "Game-Lib/ECS/Singletons/ActiveCamera.h"
#include "Game-Lib/ECS/Singletons/CharacterSingleton.h"
#include "Game-Lib/ECS/Singletons/JoltState.h"
#include "Game-Lib/ECS/Util/Transforms.h"
#include "Game-Lib/Editor/EditorHandler.h"
#include "Game-Lib/Editor/Inspector.h"
#include "Game-Lib/Editor/Viewport.h"
#include "Game-Lib/Rendering/GameRenderer.h"

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/RayCast.h>

#include <limits>

namespace Util
{
    namespace Physics
    {
        bool GetMouseWorldPosition(Editor::Viewport* viewport, vec3& outMouseWorldPosition)
        {
            ZoneScoped;
            vec2 mousePosition;
            if (viewport->GetMousePosition(mousePosition))
            {
                vec2 viewportSize = viewport->GetViewportSize();
                vec2 mouseClipPosition = (vec2(mousePosition.x / viewportSize.x, mousePosition.y / viewportSize.y) * 2.0f) - 1.0f;
                mouseClipPosition.y = -mouseClipPosition.y;

                entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
                entt::registry::context& ctx = registry->ctx();
                auto& activeCamera = ctx.get<ECS::Singletons::ActiveCamera>();

                if (activeCamera.entity == entt::null)
                    return false;

                auto& transform = registry->get<ECS::Components::Transform>(activeCamera.entity);
                auto& camera = registry->get<ECS::Components::Camera>(activeCamera.entity);

                vec4 mouseWorldPosition = camera.clipToWorld * vec4(mouseClipPosition, 0.0f, 1.0f);

                mouseWorldPosition.x /= mouseWorldPosition.w;
                mouseWorldPosition.y /= mouseWorldPosition.w;
                mouseWorldPosition.z /= mouseWorldPosition.w;

                auto& joltState = registry->ctx().get<ECS::Singletons::JoltState>();

                vec3 cameraWorldPosition = transform.GetWorldPosition();

                JPH::Vec3 start = JPH::Vec3(cameraWorldPosition.x, cameraWorldPosition.y, cameraWorldPosition.z);
                JPH::Vec3 direction = JPH::Vec3(mouseWorldPosition.x, mouseWorldPosition.y, mouseWorldPosition.z) - start;

                JPH::RRayCast ray(start, direction);
                JPH::RayCastResult hit;
                if (joltState.physicsSystem.GetNarrowPhaseQuery().CastRay(ray, hit))
                {
                    JPH::Vec3 hitPos = ray.GetPointOnRay(hit.mFraction);
                    outMouseWorldPosition = vec3(hitPos.GetX(), hitPos.GetY(), hitPos.GetZ());
                    return true;
                }
            }

            return false;
        }

        bool CastRay(JPH::PhysicsSystem& physicsSystem, vec3& start, vec3& direction, JPH::RayCastResult& result)
        {
            ZoneScoped;
            JPH::Vec3 joltStart = JPH::Vec3(start.x, start.y, start.z);
            JPH::Vec3 joltDirection = JPH::Vec3(direction.x, direction.y, direction.z);

            JPH::RRayCast ray(joltStart, joltDirection);

            return physicsSystem.GetNarrowPhaseQuery().CastRay(ray, result);
        }

        bool CastRayWorld(const vec3& start, const vec3& dir, u32 ignoreBodyID, vec3& outHitPos)
        {
            ZoneScoped;

            entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
            auto& joltState = registry->ctx().get<ECS::Singletons::JoltState>();

            // CastRay travels start + fraction*direction (fraction 0..1), so extend the (possibly
            // normalized) direction into a long segment before casting.
            constexpr f32 RAY_LENGTH = 10000.0f;
            vec3 d = glm::normalize(dir) * RAY_LENGTH;

            JPH::RRayCast ray(JPH::Vec3(start.x, start.y, start.z), JPH::Vec3(d.x, d.y, d.z));
            JPH::RayCastResult hit;

            bool didHit;
            if (ignoreBodyID != std::numeric_limits<u32>::max())
            {
                JPH::BodyID ignoredBody(ignoreBodyID);
                JPH::IgnoreSingleBodyFilter bodyFilter(ignoredBody);
                didHit = joltState.physicsSystem.GetNarrowPhaseQuery().CastRay(ray, hit, {}, {}, bodyFilter);
            }
            else
            {
                didHit = joltState.physicsSystem.GetNarrowPhaseQuery().CastRay(ray, hit);
            }

            if (!didHit)
                return false;

            JPH::Vec3 hitPos = ray.GetPointOnRay(hit.mFraction);
            outHitPos = vec3(hitPos.GetX(), hitPos.GetY(), hitPos.GetZ());
            return true;
        }
    }
}