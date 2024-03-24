#include "CameraUtil.h"

#include <Game/Util/ServiceLocator.h>
#include <Game/Rendering/GameRenderer.h>
#include <Game/ECS/Singletons/ActiveCamera.h>
#include <Game/ECS/Singletons/FreeflyingCameraSettings.h>
#include <Game/ECS/Singletons/OrbitalCameraSettings.h>
#include <Game/ECS/Components/Camera.h>
#include <Game/ECS/Util/Transforms.h>
#include <Renderer/Window.h>

#include <imgui/imgui.h>
#include <entt/entt.hpp>
#include <GLFW/glfw3.h>

namespace ECS::Util
{
    namespace CameraUtil
    {
        void SetCaptureMouse(bool capture)
        {
            entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
            entt::registry::context& ctx = registry->ctx();

            ECS::Singletons::ActiveCamera& activeCamera = ctx.get<ECS::Singletons::ActiveCamera>();
            ECS::Singletons::FreeflyingCameraSettings& freeFlyingCameraSettings = ctx.get<ECS::Singletons::FreeflyingCameraSettings>();
            ECS::Singletons::OrbitalCameraSettings& orbitalCameraSettings = ctx.get<ECS::Singletons::OrbitalCameraSettings>();

            if (activeCamera.entity == freeFlyingCameraSettings.entity)
            {
                freeFlyingCameraSettings.captureMouse = capture;
                freeFlyingCameraSettings.captureMouseHasMoved = false;
            }
            else if (activeCamera.entity == orbitalCameraSettings.entity)
            {
                orbitalCameraSettings.captureMouse = capture;
                orbitalCameraSettings.captureMouseHasMoved = false;
            }

            GameRenderer* gameRenderer = ServiceLocator::GetGameRenderer();
            Novus::Window* window = gameRenderer->GetWindow();

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

            ECS::Singletons::ActiveCamera& activeCamera = ctx.get<ECS::Singletons::ActiveCamera>();

            if (activeCamera.entity == entt::null)
                return;

            ECS::Components::Transform& transform = registry->get<ECS::Components::Transform>(activeCamera.entity);
            ECS::Components::Camera& camera = registry->get<ECS::Components::Camera>(activeCamera.entity);

            f32 fovInRadians = glm::radians(camera.fov);

            // Compute the distance the camera should be to fit the entire bounding sphere
            f32 camDistance = (radius * 2.0f) / Math::Tan(fovInRadians / 2.0f);

            ECS::TransformSystem::Get(*registry).SetLocalPosition(activeCamera.entity, position - (transform.GetLocalForward() * camDistance));
        }
    }
}
