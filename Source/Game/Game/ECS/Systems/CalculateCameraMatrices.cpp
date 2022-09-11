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
                camera.clipToView = glm::inverse(camera.worldToClip);

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
                renderResources.cameras.SetDirtyElement(camera.cameraBindSlot);
            }

            camera.dirtyPerspective = false;
            camera.dirtyView = false;
        });
	}
}