#include "FreeflyingCamera.h"

#include "Game-Lib/ECS/Singletons/CharacterSingleton.h"
#include "Game-Lib/ECS/Singletons/FreeflyingCameraSettings.h"
#include "Game-Lib/ECS/Singletons/ActiveCamera.h"
#include "Game-Lib/ECS/Components/Camera.h"
#include "Game-Lib/ECS/Util/CameraUtil.h"
#include "Game-Lib/ECS/Util/Transforms.h"
#include "Game-Lib/Input/InputActionSystem.h"
#include "Game-Lib/Util/ServiceLocator.h"
#include "Game-Lib/Rendering/GameRenderer.h"

#include <Input/InputSystem.h>
#include <Base/Util/DebugHandler.h>
#include <Renderer/Window.h>

#include <entt/entt.hpp>

namespace ECS::Systems
{
    InputActionContextHandle FreeflyingCamera::_inputContext;
    InputContextHandle FreeflyingCamera::_pointerInputContext;
    InputActionHandle FreeflyingCamera::_moveForwardAction;
    InputActionHandle FreeflyingCamera::_moveBackwardAction;
    InputActionHandle FreeflyingCamera::_moveLeftAction;
    InputActionHandle FreeflyingCamera::_moveRightAction;
    InputActionHandle FreeflyingCamera::_moveUpAction;
    InputActionHandle FreeflyingCamera::_moveDownAction;
    InputActionHandle FreeflyingCamera::_altAction;

    void FreeflyingCamera::Init(entt::registry& registry)
    {
        entt::registry::context& ctx = registry.ctx();
        auto& activeCamera = ctx.emplace<Singletons::ActiveCamera>();
        auto& settings = ctx.emplace<Singletons::FreeflyingCameraSettings>();

        // Temporarily create a camera here for debugging
        {
            entt::entity cameraEntity = registry.create();
            activeCamera.entity = cameraEntity;
            settings.entity = cameraEntity;

            auto& transform = registry.emplace<Components::Transform>(cameraEntity);
            TransformSystem::Get(registry).SetLocalPosition(cameraEntity, vec3(0, 10, -10));

            auto& camera = registry.emplace<Components::Camera>(cameraEntity);
            camera.aspectRatio = static_cast<f32>(Renderer::Settings::SCREEN_WIDTH) / static_cast<f32>(Renderer::Settings::SCREEN_HEIGHT);
            camera.pitch = 30.0f;
        }

        InputActionSystem* inputActions = ServiceLocator::GetInputActionSystem();
        _inputContext = inputActions->CreateContext("FreeFlyingCamera", GameInputPriority::Gameplay);
        inputActions->SetContextActive(_inputContext, true);

        _moveForwardAction = inputActions->RegisterAction(_inputContext, "FreeCameraForward", "Free Camera Forward", "Camera", InputBinding::Keyboard(Key::W, InputModifier::None, ModifierMatch::Any));
        _moveBackwardAction = inputActions->RegisterAction(_inputContext, "FreeCameraBackward", "Free Camera Backward", "Camera", InputBinding::Keyboard(Key::S, InputModifier::None, ModifierMatch::Any));
        _moveLeftAction = inputActions->RegisterAction(_inputContext, "FreeCameraLeft", "Free Camera Left", "Camera", InputBinding::Keyboard(Key::A, InputModifier::None, ModifierMatch::Any));
        _moveRightAction = inputActions->RegisterAction(_inputContext, "FreeCameraRight", "Free Camera Right", "Camera", InputBinding::Keyboard(Key::D, InputModifier::None, ModifierMatch::Any));
        _moveUpAction = inputActions->RegisterAction(_inputContext, "FreeCameraUp", "Free Camera Up", "Camera", InputBinding::Keyboard(Key::E, InputModifier::None, ModifierMatch::Any));
        _moveDownAction = inputActions->RegisterAction(_inputContext, "FreeCameraDown", "Free Camera Down", "Camera", InputBinding::Keyboard(Key::Q, InputModifier::None, ModifierMatch::Any));

        _altAction = inputActions->RegisterAction(_inputContext, "FreeCameraSpeedModifier", "Free Camera Speed Modifier", "Camera", InputBinding::Keyboard(Key::LeftAlt, InputModifier::None, ModifierMatch::Any), { .rebindable = false });

        inputActions->RegisterAction(_inputContext, "CaptureFreeCameraMouse", "Capture Free Camera Mouse", "Camera",
            InputBinding::Mouse(MouseButton::Right, InputModifier::None, ModifierMatch::Any), [&settings](const InputActionEvent& event)
        {
            if (event.phase == InputPhase::Pressed && !settings.captureMouse)
            {
                ECS::Util::CameraUtil::SetCaptureMouse(true);
            }

            return InputReply::Consumed;
        });

        InputSystem* inputSystem = ServiceLocator::GetInputSystem();
        _pointerInputContext = inputSystem->CreateContext("FreeFlyingCameraPointer", GameInputPriority::Gameplay, [&settings, &registry, inputActions](const InputEvent& event)
        {
            if (event.type == InputEventType::CursorMove && settings.captureMouse)
            {
                CapturedMouseMoved(registry, event.delta);
                return InputReply::Consumed;
            }

            if (event.type == InputEventType::Scroll && inputActions->IsDown(_altAction))
            {
                CapturedMouseScrolled(registry, event.delta);
                return InputReply::Consumed;
            }

            return InputReply::Ignored;
        });
        inputSystem->SetContextActive(_pointerInputContext, true);
    }

    void FreeflyingCamera::Update(entt::registry& registry, f32 deltaTime)
    {
        ZoneScopedN("ECS::FreeflyingCamera");

        entt::registry::context& ctx = registry.ctx();

        auto& activeCamera = ctx.get<Singletons::ActiveCamera>();
        auto& settings = ctx.get<Singletons::FreeflyingCameraSettings>();

        if (activeCamera.entity != settings.entity)
            return;

        auto& tSystem = ECS::TransformSystem::Get(registry);

        auto& cameraTransform = registry.get<Components::Transform>(activeCamera.entity);
        auto& camera = registry.get<Components::Camera>(activeCamera.entity);

        vec3 cameraOffset = vec3(0.0f, 0.0f, 0.0f);
        // Input
        InputActionSystem* inputActions = ServiceLocator::GetInputActionSystem();
        if (inputActions->IsDown(_moveForwardAction))
        {
            cameraOffset += cameraTransform.GetLocalForward() * settings.cameraSpeed * deltaTime;
            camera.dirtyView = true;
        }
        if (inputActions->IsDown(_moveBackwardAction))
        {
            cameraOffset += -cameraTransform.GetLocalForward() * settings.cameraSpeed * deltaTime;
            camera.dirtyView = true;
        }
        if (inputActions->IsDown(_moveLeftAction))
        {
            cameraOffset += -cameraTransform.GetLocalRight() * settings.cameraSpeed * deltaTime;
            camera.dirtyView = true;
        }
        if (inputActions->IsDown(_moveRightAction))
        {
            cameraOffset += cameraTransform.GetLocalRight() * settings.cameraSpeed * deltaTime;
            camera.dirtyView = true;
        }
        if (inputActions->IsDown(_moveUpAction))
        {
            cameraOffset += vec3(0.0f, 1.0f, 0.0f) * settings.cameraSpeed * deltaTime;
            camera.dirtyView = true;
        }
        if (inputActions->IsDown(_moveDownAction))
        {
            cameraOffset += vec3(0.0f, -1.0f, 0.0f) * settings.cameraSpeed * deltaTime;
            camera.dirtyView = true;
        }

        tSystem.AddLocalOffset(activeCamera.entity, cameraOffset);

        // Calculate rotation
        vec3 eulerAngles = vec3(camera.pitch, camera.yaw, camera.roll);

        tSystem.SetLocalRotation(activeCamera.entity, quat(glm::radians(eulerAngles)));
    }

    void FreeflyingCamera::CapturedMouseMoved(entt::registry& registry, const vec2& delta)
    {
        if (!ServiceLocator::GetInputActionSystem()->IsContextActive(_inputContext))
            return;

        entt::registry::context& ctx = registry.ctx();

        auto& activeCamera = ctx.get<Singletons::ActiveCamera>();
        auto& settings = ctx.get<Singletons::FreeflyingCameraSettings>();

        auto& camera = registry.get<Components::Camera>(activeCamera.entity);

        const f32 mouseSensitivity = ECS::Util::CameraUtil::GetMouseSensitivity();

        camera.yaw += delta.x * mouseSensitivity;

        if (camera.yaw > 360)
            camera.yaw -= 360;
        else if (camera.yaw < 0)
            camera.yaw += 360;

        camera.pitch = Math::Clamp(camera.pitch + (delta.y * mouseSensitivity), -89.0f, 89.0f);
        camera.dirtyView = true;
        settings.captureMouseHasMoved = true;
    }

    void FreeflyingCamera::CapturedMouseScrolled(entt::registry& registry, const vec2& position)
    {
        entt::registry::context& ctx = registry.ctx();
        auto& settings = ctx.get<Singletons::FreeflyingCameraSettings>();

        f32 speed = settings.cameraSpeed;
        speed = speed + ((speed / 10.0f) * position.y);
        speed = glm::max(speed, 1.0f);
        settings.cameraSpeed = speed;
    }
}
