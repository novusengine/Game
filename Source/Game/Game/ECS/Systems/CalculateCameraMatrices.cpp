#include "CalculateCameraMatrices.h"

#include "Game/ECS/Components/Transform.h"
#include "Game/ECS/Components/Camera.h"
#include "Game/Rendering/GameRenderer.h"
#include "Game/Util/ServiceLocator.h"

#include <Renderer/RenderSettings.h>

#include <entt/entt.hpp>

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
                camera.viewToWorld = transform.matrix;
                camera.worldToView = glm::inverse(transform.matrix);
            }

            if (camera.dirtyPerspective || camera.dirtyView)
            {
                camera.worldToClip = camera.viewToClip * camera.worldToView;
                camera.clipToWorld = glm::inverse(camera.worldToView) * glm::inverse(camera.viewToClip);

                // Update the GPU binding
                SafeVectorScopedWriteLock gpuCamerasLock(renderResources.cameras);
                std::vector<Camera>& gpuCameras = gpuCamerasLock.Get();
                Camera& gpuCamera = gpuCameras[camera.cameraBindSlot];

                gpuCamera.clipToView = camera.clipToView;
                gpuCamera.clipToWorld = camera.clipToWorld;

                gpuCamera.viewToClip = camera.viewToClip;
                gpuCamera.viewToWorld = camera.viewToWorld;

                gpuCamera.worldToView = camera.worldToView;
                gpuCamera.worldToClip = camera.worldToClip;

                gpuCamera.eyePosition = vec4(transform.position, 0.0f);
                gpuCamera.eyeRotation = vec4(0.0f, camera.pitch, camera.yaw, 0.0f);

                gpuCamera.nearFar = vec4(camera.nearClip, camera.farClip, 0.0f, 0.0f);

                renderResources.cameras.SetDirtyElement(camera.cameraBindSlot);
            }

            camera.dirtyPerspective = false;
            camera.dirtyView = false;
        });
	}
}