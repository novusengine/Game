#include "CalculateCameraMatrices.h"

#include "Game/ECS/Util/Transforms.h"
#include "Game/ECS/Components/Camera.h"
#include "Game/Rendering/GameRenderer.h"
#include "Game/Util/ServiceLocator.h"

#include <Base/CVarSystem/CVarSystem.h>

#include <Renderer/RenderSettings.h>

#include <entt/entt.hpp>

AutoCVar_Int CVAR_CameraLockCullingFrustum("camera.lockCullingFrustum", "Lock the frustum used for culling", 0, CVarFlags::EditCheckbox);

enum class FrustumPlane
{
    Left,
    Right,
    Bottom,
    Top,
    Near,
    Far,
};

namespace ECS::Systems
{
	void CalculateCameraMatrices::Update(entt::registry& registry, f32 deltaTime)
	{
        GameRenderer* gameRenderer = ServiceLocator::GetGameRenderer();
        RenderResources& renderResources = gameRenderer->GetRenderResources();

        auto view = registry.view<Components::Transform, Components::Camera>();

        view.each([&](Components::Transform& transform, Components::Camera& camera)
        {
            if (camera.dirtyPerspective)
            {
                camera.viewToClip = glm::perspective(glm::radians(camera.fov), camera.aspectRatio, camera.farClip, camera.nearClip);
                camera.clipToView = glm::inverse(camera.viewToClip);
            }
            if (camera.dirtyView)
            {
                mat4x4 transformMatrix = transform.GetMatrix();
                camera.viewToWorld = transformMatrix;
                camera.worldToView = glm::inverse(transformMatrix);
            }

            if (camera.dirtyPerspective || camera.dirtyView)
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
                    mat4x4& m = glm::transpose(camera.worldToClip);

                    gpuCamera.frustum[(size_t)FrustumPlane::Left] = (m[3] + m[0]);
                    gpuCamera.frustum[(size_t)FrustumPlane::Right] = (m[3] - m[0]);
                    gpuCamera.frustum[(size_t)FrustumPlane::Bottom] = (m[3] + m[1]);
                    gpuCamera.frustum[(size_t)FrustumPlane::Top] = (m[3] - m[1]);
                    gpuCamera.frustum[(size_t)FrustumPlane::Near] = (m[3] + m[2]);
                    gpuCamera.frustum[(size_t)FrustumPlane::Far] = (m[3] - m[2]);
                }

                renderResources.cameras.SetDirtyElement(camera.cameraBindSlot);
            }

            camera.dirtyPerspective = false;
            camera.dirtyView = false;
        });
	}
}