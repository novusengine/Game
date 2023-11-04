#include "FreeflyingCamera.h"

#include "Game/ECS/Singletons/FreeflyingCameraSettings.h"
#include "Game/ECS/Singletons/ActiveCamera.h"
#include "Game/ECS/Components/Camera.h"
#include "Game/ECS/Util/CameraUtil.h"
#include "Game/ECS/Util/Transforms.h"
#include "Game/Util/ServiceLocator.h"
#include "Game/Rendering/GameRenderer.h"

#include <Input/InputManager.h>
#include <Base/Util/DebugHandler.h>
#include <Renderer/Window.h>

#include <imgui/imgui.h>
#include <entt/entt.hpp>

namespace ECS::Systems
{
    KeybindGroup* FreeflyingCamera::_keybindGroup = nullptr;

	void FreeflyingCamera::Init(entt::registry& registry)
	{
        entt::registry::context& ctx = registry.ctx();
        Singletons::ActiveCamera& activeCamera = ctx.emplace<Singletons::ActiveCamera>();
        Singletons::FreeflyingCameraSettings& settings = ctx.emplace<Singletons::FreeflyingCameraSettings>();

        // Temporarily create a camera here for debugging
        {
            entt::entity cameraEntity = registry.create();
            activeCamera.entity = cameraEntity;
            Components::Transform& transform = registry.emplace<Components::Transform>(cameraEntity);
            TransformSystem::Get(registry).SetLocalPosition(cameraEntity, vec3(0, 10, -10));

            Components::Camera& camera = registry.emplace<Components::Camera>(cameraEntity);
            camera.aspectRatio = static_cast<f32>(Renderer::Settings::SCREEN_WIDTH) / static_cast<f32>(Renderer::Settings::SCREEN_HEIGHT);
            camera.pitch = 30.0f;
        }

        InputManager* inputManager = ServiceLocator::GetInputManager();
		_keybindGroup = inputManager->CreateKeybindGroup("FreeFlyingCamera", 10);
        _keybindGroup->SetActive(true);

        _keybindGroup->AddKeyboardCallback("Forward", GLFW_KEY_W, KeybindAction::Press, KeybindModifier::Any, nullptr);
        _keybindGroup->AddKeyboardCallback("Backward", GLFW_KEY_S, KeybindAction::Press, KeybindModifier::Any, nullptr);
        _keybindGroup->AddKeyboardCallback("Left", GLFW_KEY_A, KeybindAction::Press, KeybindModifier::Any, nullptr);
        _keybindGroup->AddKeyboardCallback("Right", GLFW_KEY_D, KeybindAction::Press, KeybindModifier::Any, nullptr);
        _keybindGroup->AddKeyboardCallback("Upwards", GLFW_KEY_E, KeybindAction::Press, KeybindModifier::Any, nullptr);
        _keybindGroup->AddKeyboardCallback("Downwards", GLFW_KEY_Q, KeybindAction::Press, KeybindModifier::Any, nullptr);
        _keybindGroup->AddKeyboardCallback("AltMod", GLFW_KEY_LEFT_ALT, KeybindAction::Press, KeybindModifier::Any, nullptr);
        _keybindGroup->AddKeyboardCallback("ToggleMouseCapture", GLFW_KEY_ESCAPE, KeybindAction::Press, KeybindModifier::Any, [&settings](i32 key, KeybindAction action, KeybindModifier modifier)
        {
            ECS::Util::CameraUtil::SetCaptureMouse(!settings.captureMouse);

            return true;
        });
        _keybindGroup->AddKeyboardCallback("Right Mouseclick", GLFW_MOUSE_BUTTON_RIGHT, KeybindAction::Click, KeybindModifier::Any, [&settings](i32 key, KeybindAction action, KeybindModifier modifier)
        {
            if (!settings.captureMouse)
            {
                ECS::Util::CameraUtil::SetCaptureMouse(true);

                InputManager* inputManager = ServiceLocator::GetInputManager();
                settings.prevMousePosition = vec2(inputManager->GetMousePositionX(), inputManager->GetMousePositionY());
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
            if (settings.captureMouse)
            {
                CapturedMouseScrolled(registry, vec2(xPos, yPos));
            }

            return settings.captureMouse;
        });

	}

	void FreeflyingCamera::Update(entt::registry& registry, f32 deltaTime)
	{
        entt::registry::context& ctx = registry.ctx();

        Singletons::ActiveCamera& activeCamera = ctx.get<Singletons::ActiveCamera>();
        Singletons::FreeflyingCameraSettings& settings = ctx.get<Singletons::FreeflyingCameraSettings>();

        auto& tSystem = ECS::TransformSystem::Get(registry);

        Components::Transform& cameraTransform = registry.get<Components::Transform>(activeCamera.entity);
        Components::Camera& camera = registry.get<Components::Camera>(activeCamera.entity);

        vec3 cameraOffset = vec3(0.0f, 0.0f, 0.0f);
        // Input
        if (_keybindGroup->IsKeybindPressed("Forward"_h))
        {
            cameraOffset += cameraTransform.GetLocalForward() * settings.cameraSpeed * deltaTime;
            camera.dirtyView = true;
        }
        if (_keybindGroup->IsKeybindPressed("Backward"_h))
        {
            cameraOffset += -cameraTransform.GetLocalForward() * settings.cameraSpeed * deltaTime;
            camera.dirtyView = true;
        }
        if (_keybindGroup->IsKeybindPressed("Left"_h))
        {
            cameraOffset += -cameraTransform.GetLocalRight() * settings.cameraSpeed * deltaTime;
            camera.dirtyView = true;
        }
        if (_keybindGroup->IsKeybindPressed("Right"_h))
        {
            cameraOffset += cameraTransform.GetLocalRight() * settings.cameraSpeed * deltaTime;
            camera.dirtyView = true;
        }
        if (_keybindGroup->IsKeybindPressed("Upwards"_h))
        {
            cameraOffset += vec3(0.0f, 1.0f, 0.0f) * settings.cameraSpeed * deltaTime;
            camera.dirtyView = true;
        }
        if (_keybindGroup->IsKeybindPressed("Downwards"_h))
        {
            cameraOffset += vec3(0.0f, -1.0f, 0.0f) * settings.cameraSpeed * deltaTime;
            camera.dirtyView = true;
        }

        tSystem.AddLocalOffset(activeCamera.entity, cameraOffset);

        // Calculate rotation
        vec3 eulerAngles = vec3(camera.pitch, camera.yaw, camera.roll);

        tSystem.SetLocalRotation(activeCamera.entity, quat(glm::radians(eulerAngles)));
	}

    void FreeflyingCamera::CapturedMouseMoved(entt::registry& registry, const vec2& position)
    {
        entt::registry::context& ctx = registry.ctx();

        Singletons::ActiveCamera& activeCamera = ctx.get<Singletons::ActiveCamera>();
        Singletons::FreeflyingCameraSettings& settings = ctx.get<Singletons::FreeflyingCameraSettings>();

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

    void FreeflyingCamera::CapturedMouseScrolled(entt::registry& registry, const vec2& position)
    {
        if (_keybindGroup->IsKeybindPressed("AltMod"_h))
        {
            entt::registry::context& ctx = registry.ctx();
            Singletons::FreeflyingCameraSettings& settings = ctx.get<Singletons::FreeflyingCameraSettings>();

            f32 speed = settings.cameraSpeed;
            speed = speed + ((speed / 10.0f) * position.y);
            speed = glm::max(speed, 1.0f);
            settings.cameraSpeed = speed;
        }
    }
}