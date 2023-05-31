#include "CameraUtil.h"

#include <Game/Util/ServiceLocator.h>
#include <Game/Rendering/GameRenderer.h>
#include <Game/ECS/Singletons/FreeflyingCameraSettings.h>
#include <Game/ECS/Singletons/ActiveCamera.h>
#include <Game/ECS/Components/Transform.h>
#include <Game/ECS/Components/Camera.h>

#include <Renderer/Window.h>

#include <imgui/imgui.h>

namespace ECS::Util
{
	namespace CameraUtil
	{
		void SetCaptureMouse(bool capture)
		{
            entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
            entt::registry::context& ctx = registry->ctx();

            ECS::Singletons::FreeflyingCameraSettings& settings = ctx.at<ECS::Singletons::FreeflyingCameraSettings>();

            settings.captureMouse = capture;

            GameRenderer* gameRenderer = ServiceLocator::GetGameRenderer();
            Window* window = gameRenderer->GetWindow();

            if (capture)
            {
                ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouse;
                glfwSetInputMode(window->GetWindow(), GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            }
            else
            {
                ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
                glfwSetInputMode(window->GetWindow(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            }
		}

        void CenterOnObject(const vec3& position, f32 radius)
        {
            entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
            entt::registry::context& ctx = registry->ctx();

            ECS::Singletons::ActiveCamera& activeCamera = ctx.at<ECS::Singletons::ActiveCamera>();

            ECS::Components::Transform& transform = registry->get<ECS::Components::Transform>(activeCamera.entity);
            ECS::Components::Camera& camera = registry->get<ECS::Components::Camera>(activeCamera.entity);

            f32 fovInRadians = glm::radians(camera.fov);

            // Compute the distance the camera should be to fit the entire bounding sphere
            f32 camDistance = (radius * 2.0f) / Math::Tan(fovInRadians / 2.0f);

            transform.position = position - (transform.forward * camDistance);
            transform.isDirty = true;

            camera.dirtyView = true;

            registry->get_or_emplace<ECS::Components::DirtyTransform>(activeCamera.entity);
        }
	}
}