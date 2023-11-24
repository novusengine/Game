#include "PhysicsUtil.h"

#include "Game/ECS/Components/Camera.h"
#include "Game/ECS/Singletons/ActiveCamera.h"
#include "Game/ECS/Singletons/JoltState.h"
#include "Game/ECS/Util/Transforms.h"
#include "Game/Editor/Viewport.h"

#include <entt/entt.hpp>
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/RayCast.h>

namespace Util
{
    namespace Physics
    {
        bool GetMouseWorldPosition(Editor::Viewport* viewport, vec3& outMouseWorldPosition)
        {
            vec2 mousePosition;
            if (viewport->GetMousePosition(mousePosition))
            {
                vec2 viewportSize = viewport->GetViewportSize();
                vec2 mouseClipPosition = (vec2(mousePosition.x / viewportSize.x, mousePosition.y / viewportSize.y) * 2.0f) - 1.0f;
                mouseClipPosition.y = -mouseClipPosition.y;

                entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
                entt::registry::context& ctx = registry->ctx();
                ECS::Singletons::ActiveCamera& activeCamera = ctx.get<ECS::Singletons::ActiveCamera>();

                ECS::Components::Transform& transform = registry->get<ECS::Components::Transform>(activeCamera.entity);

                ECS::Components::Camera& camera = registry->get<ECS::Components::Camera>(activeCamera.entity);

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
    }
}