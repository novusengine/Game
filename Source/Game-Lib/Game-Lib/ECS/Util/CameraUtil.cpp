#include "CameraUtil.h"

#include "Game-Lib/Util/ServiceLocator.h"
#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/ECS/Singletons/ActiveCamera.h"
#include "Game-Lib/ECS/Singletons/FreeflyingCameraSettings.h"
#include "Game-Lib/ECS/Singletons/OrbitalCameraSettings.h"
#include "Game-Lib/ECS/Components/Camera.h"
#include "Game-Lib/ECS/Util/Transforms.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Renderer/Window.h>

#include <Input/InputManager.h>

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

            auto& activeCamera = ctx.get<ECS::Singletons::ActiveCamera>();
            auto& freeFlyingCameraSettings = ctx.get<ECS::Singletons::FreeflyingCameraSettings>();
            auto& orbitalCameraSettings = ctx.get<ECS::Singletons::OrbitalCameraSettings>();

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
                i32 width, height;
                glfwGetWindowSize(window->GetWindow(), &width, &height);

                ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouse;
                glfwSetInputMode(window->GetWindow(), GLFW_CURSOR, GLFW_CURSOR_DISABLED);

                auto* inputManager = ServiceLocator::GetInputManager();
                inputManager->SetCursorVirtual(true);
            }
            else
            {
                ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
                glfwSetInputMode(window->GetWindow(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);

                auto* inputManager = ServiceLocator::GetInputManager();
                inputManager->SetCursorVirtual(false);
            }
        }

        bool IsCapturingMouse()
        {
            entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
            entt::registry::context& ctx = registry->ctx();

            auto& activeCamera = ctx.get<ECS::Singletons::ActiveCamera>();
            auto& freeFlyingCameraSettings = ctx.get<ECS::Singletons::FreeflyingCameraSettings>();
            auto& orbitalCameraSettings = ctx.get<ECS::Singletons::OrbitalCameraSettings>();

            if (activeCamera.entity == freeFlyingCameraSettings.entity)
            {
                return freeFlyingCameraSettings.captureMouse;
            }
            else if (activeCamera.entity == orbitalCameraSettings.entity)
            {
                return orbitalCameraSettings.captureMouse;
            }

            return false;
        }

        void CenterOnObject(const vec3& position, f32 radius)
        {
            entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
            entt::registry::context& ctx = registry->ctx();

            auto& activeCamera = ctx.get<ECS::Singletons::ActiveCamera>();

            if (activeCamera.entity == entt::null)
                return;

            auto& transform = registry->get<ECS::Components::Transform>(activeCamera.entity);
            auto& camera = registry->get<ECS::Components::Camera>(activeCamera.entity);

            f32 fovInRadians = glm::radians(camera.fov) * 0.6f;

            // Compute the distance the camera should be to fit the entire bounding sphere
            f32 camDistance = (radius * 2.0f) / Math::Tan(fovInRadians / 2.0f);

            ECS::TransformSystem::Get(*registry).SetLocalPosition(activeCamera.entity, position - (transform.GetLocalForward() * camDistance));
        }
    }
}
