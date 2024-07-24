#include "CalculateCameraMatrices.h"

#include "Game/ECS/Components/Camera.h"
#include "Game/ECS/Singletons/ActiveCamera.h"
#include "Game/ECS/Util/Transforms.h"
#include "Game/Rendering/GameRenderer.h"
#include "Game/Util/ServiceLocator.h"

#include <Base/CVarSystem/CVarSystem.h>

#include <Renderer/RenderSettings.h>

#include <entt/entt.hpp>

AutoCVar_Int CVAR_CameraLockCullingFrustum(CVarCategory::Client | CVarCategory::Rendering, "cameraLockCullingFrustum", "Lock the frustum used for culling", 0, CVarFlags::EditCheckbox);

namespace ECS::Systems
{
    inline vec4 EncodePlane(vec3 position, vec3 normal)
    {
        vec3 normalizedNormal = glm::normalize(normal);
        vec4 result = vec4(normalizedNormal, glm::dot(normalizedNormal, position));
        return result;
    }

    void CalculateCameraMatrices::Update(entt::registry& registry, f32 deltaTime)
    {
        GameRenderer* gameRenderer = ServiceLocator::GetGameRenderer();
        RenderResources& renderResources = gameRenderer->GetRenderResources();

        entt::registry::context& ctx = registry.ctx();
        auto& activeCamera = ctx.get<ECS::Singletons::ActiveCamera>();

        auto view = registry.view<Components::Transform, Components::Camera>();
        view.each([&](entt::entity e, Components::Transform& transform, Components::Camera& camera)
        {
            if (e != activeCamera.entity)
            {
                // TODO: Multiple cameras (picture-in-picture I guess?) would need to change this
                return;
            }

            vec2 renderSize = gameRenderer->GetRenderer()->GetRenderSize();
            camera.aspectRatio = static_cast<f32>(Renderer::Settings::SCREEN_WIDTH) / static_cast<f32>(Renderer::Settings::SCREEN_HEIGHT);

            {
                f32 fov = glm::radians(camera.fov) * 0.6f;
                camera.viewToClip = glm::perspective(fov, camera.aspectRatio, camera.farClip, camera.nearClip);
                camera.clipToView = glm::inverse(camera.viewToClip);
            }

            {
                mat4x4 transformMatrix = transform.GetMatrix();
                camera.viewToWorld = transformMatrix;
                camera.worldToView = glm::inverse(transformMatrix);
            }

            {
                camera.worldToClip = camera.viewToClip * camera.worldToView;
                camera.clipToWorld = glm::inverse(camera.worldToView) * glm::inverse(camera.viewToClip);

                // Update the GPU binding
                std::vector<Camera>& gpuCameras = renderResources.cameras.Get();
                Camera& gpuCamera = gpuCameras[camera.cameraBindSlot];

                gpuCamera.clipToView = camera.clipToView;
                gpuCamera.clipToWorld = camera.clipToWorld;

                gpuCamera.viewToClip = camera.viewToClip;
                gpuCamera.viewToWorld = camera.viewToWorld;

                gpuCamera.worldToView = camera.worldToView;
                gpuCamera.worldToClip = camera.worldToClip;

                gpuCamera.eyePosition = vec4(transform.GetWorldPosition(), 0.0f);
                gpuCamera.eyeRotation = vec4(0.0f, camera.pitch, camera.yaw, 0.0f);

                gpuCamera.nearFar = vec4(camera.nearClip, camera.farClip, 0.0f, 0.0f);

                if (CVAR_CameraLockCullingFrustum.Get() == 0)
                {
                    mat4x4 m = glm::transpose(camera.worldToClip);

                    glm::vec3 Front = glm::vec3(0, 0, 1);
                    glm::vec3 Right = glm::vec3(1, 0, 0);
                    glm::vec3 Up = glm::vec3(0, 1, 0);

                    vec3 position = transform.GetWorldPosition();
                    mat4x4 transformMatrix = transform.GetMatrix();

                    Front = glm::vec3(transformMatrix * glm::vec4(Front, 0.f));
                    Right = glm::vec3(transformMatrix * glm::vec4(Right, 0.f));
                    Up = glm::vec3(transformMatrix * glm::vec4(Up, 0.f));

                    f32 fov = glm::radians(camera.fov) * 0.6f;
                    const float halfVSide = camera.farClip * tanf(fov * .5f);
                    const float halfHSide = halfVSide * camera.aspectRatio;
                    const glm::vec3 frontMultFar = camera.farClip * Front;

                    gpuCamera.frustum[(size_t)FrustumPlane::Near] = EncodePlane(position + camera.nearClip * Front, Front);
                    gpuCamera.frustum[(size_t)FrustumPlane::Far] = EncodePlane(position + frontMultFar, -Front);
                    gpuCamera.frustum[(size_t)FrustumPlane::Right] = EncodePlane(position,glm::cross(Up, frontMultFar - Right * halfHSide));
                    gpuCamera.frustum[(size_t)FrustumPlane::Left] = EncodePlane(position,glm::cross(frontMultFar + Right * halfHSide, Up));
                    gpuCamera.frustum[(size_t)FrustumPlane::Top] = EncodePlane(position,glm::cross(frontMultFar - Up * halfVSide, Right));
                    gpuCamera.frustum[(size_t)FrustumPlane::Bottom] = EncodePlane(position,glm::cross(Right, frontMultFar + Up * halfVSide));
                }

                renderResources.cameras.SetDirtyElement(camera.cameraBindSlot);
            }

            camera.dirtyPerspective = false;
            camera.dirtyView = false;
        });
    }
}
