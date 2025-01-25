#include "OrbitalCamera.h"

#include "Game-Lib/ECS/Components/MovementInfo.h"
#include "Game-Lib/ECS/Components/NetworkedEntity.h"
#include "Game-Lib/ECS/Singletons/CharacterSingleton.h"
#include "Game-Lib/ECS/Singletons/OrbitalCameraSettings.h"
#include "Game-Lib/ECS/Singletons/ActiveCamera.h"
#include "Game-Lib/ECS/Components/Camera.h"
#include "Game-Lib/ECS/Util/CameraUtil.h"
#include "Game-Lib/ECS/Util/Transforms.h"
#include "Game-Lib/Util/ServiceLocator.h"
#include "Game-Lib/Rendering/GameRenderer.h"

#include <Base/CVarSystem/CVarSystem.h>
#include <Base/Util/DebugHandler.h>
#include <Input/InputManager.h>
#include <Renderer/Window.h>

#include <entt/entt.hpp>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <imgui/imgui.h>

AutoCVar_Float CVAR_CameraZoomDistance(CVarCategory::Client, "cameraZoomDistance", "The current zoom distance for the camera", 12, CVarFlags::EditFloatDrag);
AutoCVar_Float CVAR_CameraZoomMaxDistance(CVarCategory::Client, "cameraZoomMaxDistance", "The max zoom distance for the camera", 30.0f, CVarFlags::EditFloatDrag);

namespace ECS::Systems
{
    KeybindGroup* OrbitalCamera::_keybindGroup = nullptr;

    void OrbitalCamera::Init(entt::registry& registry)
    {
        entt::registry::context& ctx = registry.ctx();
        auto& activeCamera = ctx.emplace<Singletons::ActiveCamera>();
        auto& settings = ctx.emplace<Singletons::OrbitalCameraSettings>();

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

        f32 zoomDistance = Math::Clamp(CVAR_CameraZoomDistance.GetFloat(), 0.0f, CVAR_CameraZoomMaxDistance.GetFloat());
        CVAR_CameraZoomDistance.Set(zoomDistance);

        settings.cameraCurrentZoomOffset = vec3(0.0f, 1.0f, 1.0f) * vec3(1.0f, 1.8f, -zoomDistance);
        settings.cameraTargetZoomOffset = settings.cameraCurrentZoomOffset;

        InputManager* inputManager = ServiceLocator::GetInputManager();
        _keybindGroup = inputManager->CreateKeybindGroup("OrbitalCamera", 10);
        _keybindGroup->SetActive(false);

        _keybindGroup->AddKeyboardCallback("AltMod", GLFW_KEY_LEFT_ALT, KeybindAction::Press, KeybindModifier::Any, nullptr);
        _keybindGroup->AddKeyboardCallback("Left Mouseclick", GLFW_MOUSE_BUTTON_LEFT, KeybindAction::Click, KeybindModifier::Any, [&settings](i32 key, KeybindAction action, KeybindModifier modifier)
        {
            if (action == KeybindAction::Press)
            {
                if (!settings.mouseRightDown)
                {
                    ECS::Util::CameraUtil::SetCaptureMouse(true);

                    InputManager* inputManager = ServiceLocator::GetInputManager();
                    settings.prevMousePosition = vec2(inputManager->GetMousePositionX(), inputManager->GetMousePositionY());
                }

                settings.mouseLeftDown = true;
            }
            else
            {
                if (!settings.mouseRightDown)
                {
                    ECS::Util::CameraUtil::SetCaptureMouse(false);
                }

                settings.mouseLeftDown = false;
            }

            return true;
        });
        _keybindGroup->AddKeyboardCallback("Right Mouseclick", GLFW_MOUSE_BUTTON_RIGHT, KeybindAction::Click, KeybindModifier::Any, [&settings](i32 key, KeybindAction action, KeybindModifier modifier)
        {
            if (action == KeybindAction::Press)
            {
                if (!settings.mouseLeftDown)
                {
                    ECS::Util::CameraUtil::SetCaptureMouse(true);

                    InputManager* inputManager = ServiceLocator::GetInputManager();
                    settings.prevMousePosition = vec2(inputManager->GetMousePositionX(), inputManager->GetMousePositionY());
                }

                settings.mouseRightDown = true;
            }
            else
            {
                if (!settings.mouseLeftDown)
                {
                    ECS::Util::CameraUtil::SetCaptureMouse(false);
                }

                settings.mouseRightDown = false;
            }

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
                f32 zoomDistance = Math::Clamp(CVAR_CameraZoomDistance.GetFloat() + static_cast<i8>(-yPos), 0.0f, CVAR_CameraZoomMaxDistance.GetFloat());
                CVAR_CameraZoomDistance.Set(zoomDistance);

                settings.cameraTargetZoomOffset = vec3(0.0f, 1.0f, 1.0f) * vec3(1.0f, 1.8f, -zoomDistance);
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
        ZoneScopedN("ECS::OrbitalCamera");

        entt::registry::context& ctx = registry.ctx();

        auto& activeCamera = ctx.get<Singletons::ActiveCamera>();
        auto& settings = ctx.get<Singletons::OrbitalCameraSettings>();
        auto& characterSingleton = ctx.get<Singletons::CharacterSingleton>();

        if (activeCamera.entity != settings.entity)
            return;

        auto& tSystem = ECS::TransformSystem::Get(registry);

        auto& cameraTransform = registry.get<Components::Transform>(activeCamera.entity);
        auto& camera = registry.get<Components::Camera>(activeCamera.entity);

        settings.cameraZoomProgress += settings.cameraZoomSpeed * deltaTime;
        settings.cameraZoomProgress = glm::clamp(settings.cameraZoomProgress, 0.0f, 1.0f);
        settings.cameraCurrentZoomOffset = glm::mix(settings.cameraCurrentZoomOffset, settings.cameraTargetZoomOffset, settings.cameraZoomProgress);

        auto& characterControllerTransform = registry.get<Components::Transform>(characterSingleton.controllerEntity);
        const mat4x4& characterControllerMatrix = characterControllerTransform.GetMatrix(); // This is the point we want to rotate around

        vec3 eulerAngles = vec3(glm::radians(camera.pitch), glm::radians(camera.yaw), glm::radians(camera.roll));

        f32 cameraHeightOffset = settings.cameraCurrentZoomOffset.y;
        f32 cameraZoomDistance = settings.cameraCurrentZoomOffset.z;

        // Update the camera
        vec3 resultPosition;
        quat resultRotation;
        updateCamera(characterControllerMatrix, eulerAngles, cameraHeightOffset, cameraZoomDistance, resultPosition, resultRotation);

        vec3 resultRotationEuler = glm::eulerAngles(resultRotation);

        if (settings.mouseRightDown && characterSingleton.moverEntity != entt::null)
        {
            auto& movementInfo = registry.get<Components::MovementInfo>(characterSingleton.moverEntity);
            auto& networkedEntity = registry.get<Components::NetworkedEntity>(characterSingleton.moverEntity);

            glm::mat3 rotationMatrix = glm::mat3_cast(resultRotation);
            f32 yaw = glm::pi<f32>() + glm::radians(camera.yaw);

            movementInfo.yaw = yaw;
            networkedEntity.positionOrRotationIsDirty = true;
        }

        tSystem.SetWorldRotation(activeCamera.entity, resultRotation);
        tSystem.SetWorldPosition(activeCamera.entity, resultPosition);

        camera.dirtyView = true;
    }

    void OrbitalCamera::CapturedMouseMoved(entt::registry& registry, const vec2& position)
    {
        if (!_keybindGroup->IsActive())
            return;

        entt::registry::context& ctx = registry.ctx();

        auto& activeCamera = ctx.get<Singletons::ActiveCamera>();
        auto& settings = ctx.get<Singletons::OrbitalCameraSettings>();

        auto& camera = registry.get<Components::Camera>(activeCamera.entity);

        if (settings.captureMouseHasMoved)
        {
            vec2 deltaPosition = settings.prevMousePosition - position;

            camera.yaw += -deltaPosition.x * settings.mouseSensitivity;

            if (camera.yaw >= 360.0f)
                camera.yaw -= 360.0f;
            else if (camera.yaw < 0.0f)
                camera.yaw += 360.0f;

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
        auto& characterSingleton = ctx.get<Singletons::CharacterSingleton>();
        auto& movementInfo = registry.get<Components::MovementInfo>(characterSingleton.moverEntity);

        f32 speed = movementInfo.speed;
        speed = speed + ((speed / 20.0f) * position.y);
        speed = glm::max(speed, 7.1111f);
        movementInfo.speed = speed;
    }
}