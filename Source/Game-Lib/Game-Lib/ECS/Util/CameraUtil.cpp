#include "CameraUtil.h"

#include "Game-Lib/Util/ServiceLocator.h"
#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/ECS/Singletons/ActiveCamera.h"
#include "Game-Lib/ECS/Singletons/FreeflyingCameraSettings.h"
#include "Game-Lib/ECS/Singletons/OrbitalCameraSettings.h"
#include "Game-Lib/ECS/Components/Camera.h"
#include "Game-Lib/ECS/Util/Transforms.h"

#include <Base/CVarSystem/CVarSystem.h>

#include <Renderer/Window.h>

#include <Input/InputManager.h>

#include <imgui/imgui.h>
#include <entt/entt.hpp>
#include <GLFW/glfw3.h>

#include <glm/gtx/euler_angles.hpp>

AutoCVar_Float CVAR_CameraMouseSensitivity(CVarCategory::Client, "cameraMouseSensitivity", "Mouse sensitivity multiplier used by captured camera rotation", 1.0f, CVarFlags::EditFloatDrag);

namespace
{
    constexpr f32 BASE_CAMERA_MOUSE_SENSITIVITY = 0.05f;
}

namespace ECS::Util
{
    namespace CameraUtil
    {
        void SetCaptureMouse(bool capture)
        {
            SetCaptureMouse(capture, ServiceLocator::GetInputManager()->GetMousePosition());
        }

        void SetCaptureMouse(bool capture, const vec2& restorePosition)
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

                if (!capture)
                    orbitalCameraSettings.captureMousePending = false;
            }

            GameRenderer* gameRenderer = ServiceLocator::GetGameRenderer();
            Novus::Window* window = gameRenderer->GetWindow();
            InputManager* inputManager = ServiceLocator::GetInputManager();

            if (capture)
            {
                ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouse;
                inputManager->SetCursorVirtual(true);

                // Hide before entering disabled mode. GLFW centers the physical cursor while
                // enabling disabled mode on Windows, so a direct NORMAL -> DISABLED transition
                // can expose a one-frame center flash during rapid clicks.
                glfwSetInputMode(window->GetWindow(), GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
                glfwSetInputMode(window->GetWindow(), GLFW_CURSOR, GLFW_CURSOR_DISABLED);

                if (glfwRawMouseMotionSupported())
                    glfwSetInputMode(window->GetWindow(), GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
            }
            else
            {
                if (glfwRawMouseMotionSupported())
                    glfwSetInputMode(window->GetWindow(), GLFW_RAW_MOUSE_MOTION, GLFW_FALSE);

                // Leaving disabled mode restores GLFW's saved position. Keep the cursor hidden
                // for that transition, then apply the gesture's authoritative restore position
                // before making it visible again.
                glfwSetInputMode(window->GetWindow(), GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
                gameRenderer->RestoreCursorPosition(restorePosition);
                glfwSetInputMode(window->GetWindow(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                inputManager->SetCursorVirtual(false);
                ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
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

        f32 GetMouseSensitivity()
        {
            const f32 sensitivityMultiplier = glm::max(CVAR_CameraMouseSensitivity.GetFloat(), 0.0f);
            return BASE_CAMERA_MOUSE_SENSITIVITY * sensitivityMultiplier;
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
