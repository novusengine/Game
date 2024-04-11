#include "OrbitalCamera.h"

#include "Game/ECS/Singletons/CharacterSingleton.h"
#include "Game/ECS/Singletons/OrbitalCameraSettings.h"
#include "Game/ECS/Singletons/ActiveCamera.h"
#include "Game/ECS/Components/Camera.h"
#include "Game/ECS/Util/CameraUtil.h"
#include "Game/ECS/Util/Transforms.h"
#include "Game/Util/ServiceLocator.h"
#include "Game/Rendering/GameRenderer.h"

#include <Input/InputManager.h>
#include <Base/Util/DebugHandler.h>
#include <Renderer/Window.h>

#include <entt/entt.hpp>
#include <imgui/imgui.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/euler_angles.hpp>

namespace ECS::Systems
{
    KeybindGroup* OrbitalCamera::_keybindGroup = nullptr;

    void OrbitalCamera::Init(entt::registry& registry)
    {
        entt::registry::context& ctx = registry.ctx();
        Singletons::ActiveCamera& activeCamera = ctx.emplace<Singletons::ActiveCamera>();
        Singletons::OrbitalCameraSettings& settings = ctx.emplace<Singletons::OrbitalCameraSettings>();

        // Temporarily create a camera here for debugging
        {
            entt::entity cameraEntity = registry.create();
            settings.entity = cameraEntity;

            registry.emplace<Components::Transform>(cameraEntity);
            Components::Camera& camera = registry.emplace<Components::Camera>(cameraEntity);
            camera.aspectRatio = static_cast<f32>(Renderer::Settings::SCREEN_WIDTH) / static_cast<f32>(Renderer::Settings::SCREEN_HEIGHT);
            camera.pitch = 30.0f;
            camera.yaw = 180.0f;
        }

        settings.cameraCurrentZoomOffset = (vec3(0.0f, 1.0f, 1.0f) * vec3(1.0f, settings.GetZoomLevel()));
        settings.cameraTargetZoomOffset = settings.cameraCurrentZoomOffset;

        InputManager* inputManager = ServiceLocator::GetInputManager();
        _keybindGroup = inputManager->CreateKeybindGroup("OrbitalCamera", 10);
        _keybindGroup->SetActive(false);

        _keybindGroup->AddKeyboardCallback("AltMod", GLFW_KEY_LEFT_ALT, KeybindAction::Press, KeybindModifier::Any, nullptr);
        _keybindGroup->AddKeyboardCallback("Left Mouseclick", GLFW_MOUSE_BUTTON_LEFT, KeybindAction::Click, KeybindModifier::Any, [&settings](i32 key, KeybindAction action, KeybindModifier modifier)
        {
            if (action == KeybindAction::Press)
            {
                ECS::Util::CameraUtil::SetCaptureMouse(true);

                InputManager* inputManager = ServiceLocator::GetInputManager();
                settings.prevMousePosition = vec2(inputManager->GetMousePositionX(), inputManager->GetMousePositionY());
            }
            else
            {
                ECS::Util::CameraUtil::SetCaptureMouse(false);
            }

            return true;
        });
        _keybindGroup->AddKeyboardCallback("Right Mouseclick", GLFW_MOUSE_BUTTON_RIGHT, KeybindAction::Click, KeybindModifier::Any, [&settings](i32 key, KeybindAction action, KeybindModifier modifier)
        {
            // TODO : Implement rotating the entity if we are the mover of the entity we are tracking
            return true;
        });
        _keybindGroup->AddMousePositionCallback([&settings, &registry](f32 xPos, f32 yPos)
        {
            if (settings.captureMouse)
            {
                CapturedMouseMoved(registry, vec2(xPos, yPos));
            }

            return settings.captureMouse;
        });
        _keybindGroup->AddMouseScrollCallback([&settings, &registry](f32 xPos, f32 yPos)
        {
            if (_keybindGroup->IsKeybindPressed("AltMod"_h))
            {
                CapturedMouseScrolled(registry, vec2(xPos, yPos));
            }
            else
            {
                settings.cameraZoomLevel = Math::Clamp(settings.cameraZoomLevel + static_cast<i8>(-yPos), 0, 4);
                settings.cameraTargetZoomOffset = vec3(0.0f, 1.0f, 1.0f) * vec3(1.0f, settings.GetZoomLevel());
                settings.cameraZoomProgress = 0.0f;
            }

            return true;
        });
    }

    void updateCamera(const mat4x4& characterControllerMatrix, vec3 eulerAngles, f32 heightOffset, f32 zoomDistance, vec3& resultPosition, quat& resultRotation)
    {
        // Height offset matrix to move the rotation point up by the specified height
        mat4x4 heightOffsetMatrix = translate(mat4x4(1.0f), vec3(0.0f, heightOffset, 0.0f));

        // Create rotation matrix from Euler angles
        mat4x4 rotationMatrix = glm::eulerAngleYXZ(eulerAngles.y, eulerAngles.x, eulerAngles.z);

        // Translation matrix to move the camera back to the correct offset
        mat4x4 translationMatrix = glm::translate(mat4x4(1.0f), vec3(0.0f, 0.0f, zoomDistance));

        // Combine transformations: first rotate, then translate
        mat4x4 cameraMatrix = characterControllerMatrix * heightOffsetMatrix * rotationMatrix * translationMatrix;

        // Extract position and rotation from the transformation matrix
        resultPosition = vec3(cameraMatrix[3]);
        resultRotation = normalize(quat_cast(cameraMatrix));
    }

    void OrbitalCamera::Update(entt::registry& registry, f32 deltaTime)
    {
        entt::registry::context& ctx = registry.ctx();

        Singletons::ActiveCamera& activeCamera = ctx.get<Singletons::ActiveCamera>();
        Singletons::OrbitalCameraSettings& settings = ctx.get<Singletons::OrbitalCameraSettings>();
        auto& characterSingleton = ctx.get<Singletons::CharacterSingleton>();

        if (activeCamera.entity != settings.entity)
            return;

        auto& tSystem = ECS::TransformSystem::Get(registry);

        Components::Transform& cameraTransform = registry.get<Components::Transform>(activeCamera.entity);
        Components::Camera& camera = registry.get<Components::Camera>(activeCamera.entity);

        settings.cameraZoomProgress += settings.cameraZoomSpeed * deltaTime;
        settings.cameraZoomProgress = glm::clamp(settings.cameraZoomProgress, 0.0f, 1.0f);
        settings.cameraCurrentZoomOffset = glm::mix(settings.cameraCurrentZoomOffset, settings.cameraTargetZoomOffset, settings.cameraZoomProgress);

        Components::Transform& characterControllerTransform = registry.get<Components::Transform>(characterSingleton.entity);
        const mat4x4& characterControllerMatrix = characterControllerTransform.GetMatrix(); // This is the point we want to rotate around

        vec3 eulerAngles = vec3(glm::radians(camera.pitch), glm::radians(camera.yaw), glm::radians(camera.roll));

        f32 cameraHeightOffset = settings.cameraCurrentZoomOffset.y;
        f32 cameraZoomDistance = settings.cameraCurrentZoomOffset.z;

        // Update the camera
        vec3 resultPosition;
        quat resultRotation;
        updateCamera(characterControllerMatrix, eulerAngles, cameraHeightOffset, cameraZoomDistance, resultPosition, resultRotation);

        vec3 resultRotationEuler = glm::eulerAngles(resultRotation);

        tSystem.SetWorldRotation(activeCamera.entity, resultRotation);
        tSystem.SetWorldPosition(activeCamera.entity, resultPosition);

        camera.dirtyView = true;
    }

    void OrbitalCamera::CapturedMouseMoved(entt::registry& registry, const vec2& position)
    {
        if (!_keybindGroup->IsActive())
            return;

        entt::registry::context& ctx = registry.ctx();

        Singletons::ActiveCamera& activeCamera = ctx.get<Singletons::ActiveCamera>();
        Singletons::OrbitalCameraSettings& settings = ctx.get<Singletons::OrbitalCameraSettings>();

        Components::Camera& camera = registry.get<Components::Camera>(activeCamera.entity);

        if (settings.captureMouseHasMoved)
        {
            vec2 deltaPosition = settings.prevMousePosition - position;

            camera.yaw += -deltaPosition.x * settings.mouseSensitivity;

            if (camera.yaw > 360)
                camera.yaw -= 360;
            else if (camera.yaw < 0)
                camera.yaw += 360;

            camera.pitch = Math::Clamp(camera.pitch - (deltaPosition.y * settings.mouseSensitivity), -89.0f, 89.0f);
            camera.dirtyView = true;
        }
        else
            settings.captureMouseHasMoved = true;

        settings.prevMousePosition = position;
    }

    void OrbitalCamera::CapturedMouseScrolled(entt::registry& registry, const vec2& position)
    {
        entt::registry::context& ctx = registry.ctx();
        Singletons::CharacterSingleton& characterSingleton = ctx.get<Singletons::CharacterSingleton>();

        f32 speed = characterSingleton.speed;
        speed = speed + ((speed / 20.0f) * position.y);
        speed = glm::max(speed, 7.1111f);
        characterSingleton.speed = speed;
    }
}