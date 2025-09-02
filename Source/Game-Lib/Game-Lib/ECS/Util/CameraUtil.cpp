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

#include <glm/gtx/euler_angles.hpp>

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
                ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouse;
                glfwSetInputMode(window->GetWindow(), GLFW_CURSOR, GLFW_CURSOR_DISABLED);

                ServiceLocator::GetInputManager()->SetCursorVirtual(true);
            }
            else
            {
                ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
                glfwSetInputMode(window->GetWindow(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                ServiceLocator::GetInputManager()->SetCursorVirtual(false);
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

        void CalculatePosRotForMatrix(const mat4x4& targetMatrix, const vec3& cameraEulerAngles, f32 cameraHeightOffset, f32 cameraZoomDistance, vec3& resultPosition, quat& resultRotation)
        {
            // Height offset matrix to move the rotation point up by the specified height
            mat4x4 heightOffsetMatrix = glm::translate(mat4x4(1.0f), vec3(0.0f, cameraHeightOffset, 0.0f));

            // Create rotation matrix from Euler angles
            mat4x4 rotationMatrix = glm::eulerAngleYXZ(cameraEulerAngles.y, cameraEulerAngles.x, cameraEulerAngles.z);

            // Translation matrix to move the camera back to the correct offset
            mat4x4 translationMatrix = glm::translate(mat4x4(1.0f), vec3(0.0f, 0.0f, cameraZoomDistance));

            // Combine transformations: first rotate, then translate
            mat4x4 cameraMatrix = targetMatrix * heightOffsetMatrix * rotationMatrix * translationMatrix;

            // Extract position and rotation from the transformation matrix
            resultPosition = vec3(cameraMatrix[3]);
            resultRotation = normalize(quat_cast(cameraMatrix));
        }
    }
}
