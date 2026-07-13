#include "OrbitalCamera.h"

#include "Game-Lib/ECS/Components/MovementInfo.h"
#include "Game-Lib/ECS/Components/Unit.h"
#include "Game-Lib/ECS/Singletons/CharacterSingleton.h"
#include "Game-Lib/ECS/Singletons/JoltState.h"
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
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <imgui/imgui.h>

AutoCVar_Float CVAR_CameraZoomDistance(CVarCategory::Client, "cameraZoomDistance", "The current zoom distance for the camera", 12, CVarFlags::EditFloatDrag);
AutoCVar_Float CVAR_CameraZoomMaxDistance(CVarCategory::Client, "cameraZoomMaxDistance", "The max zoom distance for the camera", 30.0f, CVarFlags::EditFloatDrag);
AutoCVar_Int CVAR_CameraCollision(CVarCategory::Client, "cameraCollision", "Enables orbital camera collision against static world geometry", 1, CVarFlags::EditCheckbox);
AutoCVar_Float CVAR_CameraCollisionPadding(CVarCategory::Client, "cameraCollisionPadding", "Distance to keep between the orbital camera and hit geometry", 0.25f, CVarFlags::EditFloatDrag);
AutoCVar_Float CVAR_CameraCollisionMinDistance(CVarCategory::Client, "cameraCollisionMinDistance", "Minimum distance the orbital camera can be pushed from its focus point", 0.35f, CVarFlags::EditFloatDrag);
AutoCVar_Float CVAR_CameraCollisionReturnSpeed(CVarCategory::Client, "cameraCollisionReturnSpeed", "How quickly the orbital camera eases back after collision clears", 8.0f, CVarFlags::EditFloatDrag);
AutoCVar_Float CVAR_CameraMouseDragThreshold(CVarCategory::Client, "cameraMouseDragThreshold", "Mouse displacement in screen pixels before a click becomes a camera drag", 8.0f, CVarFlags::EditFloatDrag);
AutoCVar_Float CVAR_CameraClickGracePeriod(CVarCategory::Client, "cameraClickGracePeriod", "Time in milliseconds before mouse movement can turn a click into a camera drag", 50.0f, CVarFlags::EditFloatDrag);

namespace
{
    constexpr f32 DEFAULT_CAMERA_PITCH = 30.0f;
    constexpr f32 DEFAULT_CAMERA_ZOOM_DISTANCE = 12.0f;

    void BeginMouseGesture(ECS::Singletons::OrbitalCameraSettings& settings)
    {
        GameRenderer* gameRenderer = ServiceLocator::GetGameRenderer();
        Novus::Window* window = gameRenderer->GetWindow();
        InputManager* inputManager = ServiceLocator::GetInputManager();

        gameRenderer->CancelCursorRestore();

        f64 mouseX;
        f64 mouseY;
        glfwGetCursorPos(window->GetWindow(), &mouseX, &mouseY);
        const vec2 physicalMousePosition(static_cast<f32>(mouseX), static_cast<f32>(mouseY));
        inputManager->SetMousePosition(physicalMousePosition.x, physicalMousePosition.y);

        settings.captureMousePending = true;
        settings.captureMouseHasMoved = false;
        settings.captureMouseWasDragged = false;
        settings.prevMousePosition = physicalMousePosition;
        settings.captureStartMousePosition = settings.prevMousePosition;
        settings.captureRestoreMousePosition = settings.captureStartMousePosition;
        settings.captureStartTime = glfwGetTime();
    }

    class StaticBroadPhaseFilter final : public JPH::BroadPhaseLayerFilter
    {
    public:
        bool ShouldCollide(JPH::BroadPhaseLayer layer) const override
        {
            return layer == Jolt::BroadPhaseLayers::NON_MOVING;
        }
    };

    class StaticObjectLayerFilter final : public JPH::ObjectLayerFilter
    {
    public:
        bool ShouldCollide(JPH::ObjectLayer layer) const override
        {
            return layer == Jolt::Layers::NON_MOVING;
        }
    };

    bool TryGetCameraCollisionDistance(ECS::Singletons::JoltState& joltState, const vec3& focusPosition, const vec3& desiredPosition, f32& outDistance)
    {
        const vec3 focusToCamera = desiredPosition - focusPosition;
        const f32 distanceSquared = glm::dot(focusToCamera, focusToCamera);
        if (distanceSquared <= 0.000001f)
            return false;

        const f32 distance = glm::sqrt(distanceSquared);
        const JPH::RRayCast ray(
            JPH::RVec3(focusPosition.x, focusPosition.y, focusPosition.z),
            JPH::Vec3(focusToCamera.x, focusToCamera.y, focusToCamera.z));

        const StaticBroadPhaseFilter broadPhaseFilter;
        const StaticObjectLayerFilter objectLayerFilter;
        const JPH::BodyFilter bodyFilter;

        JPH::RayCastResult hit;
        if (!joltState.physicsSystem.GetNarrowPhaseQuery().CastRay(ray, hit, broadPhaseFilter, objectLayerFilter, bodyFilter))
            return false;

        const f32 padding = glm::max(CVAR_CameraCollisionPadding.GetFloat(), 0.0f);
        const f32 minDistance = glm::max(CVAR_CameraCollisionMinDistance.GetFloat(), 0.0f);
        outDistance = glm::clamp((hit.mFraction * distance) - padding, minDistance, distance);
        return true;
    }
}

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
            camera.pitch = DEFAULT_CAMERA_PITCH;
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
                    BeginMouseGesture(settings);

                settings.mouseLeftDown = true;
            }
            else
            {
                if (!settings.mouseRightDown)
                {
                    if (settings.captureMouse)
                        ECS::Util::CameraUtil::SetCaptureMouse(false, settings.captureRestoreMousePosition);
                    else
                        settings.captureMousePending = false;
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
                    BeginMouseGesture(settings);

                settings.mouseRightDown = true;
            }
            else
            {
                if (!settings.mouseLeftDown)
                {
                    if (settings.captureMouse)
                        ECS::Util::CameraUtil::SetCaptureMouse(false, settings.captureRestoreMousePosition);
                    else
                        settings.captureMousePending = false;
                }

                settings.mouseRightDown = false;
            }

            return true;
        });
        _keybindGroup->AddMousePositionCallback([&settings, &registry](f32 xPos, f32 yPos)
        {
            if (settings.captureMousePending)
            {
                const vec2 position(xPos, yPos);
                settings.prevMousePosition = position;

                const f64 clickGracePeriod = static_cast<f64>(glm::max(CVAR_CameraClickGracePeriod.GetFloat(), 0.0f)) / 1000.0;
                if (glfwGetTime() - settings.captureStartTime < clickGracePeriod)
                    return true;

                const vec2 displacement = position - settings.captureStartMousePosition;
                const f32 dragThreshold = glm::max(CVAR_CameraMouseDragThreshold.GetFloat(), 0.0f);
                if (glm::dot(displacement, displacement) >= dragThreshold * dragThreshold)
                {
                    settings.captureMousePending = false;
                    settings.captureMouseWasDragged = true;
                    settings.captureRestoreMousePosition = position;
                    ECS::Util::CameraUtil::SetCaptureMouse(true, settings.captureRestoreMousePosition);
                }

                return true;
            }

            if (settings.captureMouse)
                CapturedMouseMoved(registry, vec2(xPos, yPos));

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

    void OrbitalCamera::ResetForNewWorld(entt::registry& registry)
    {
        auto& settings = registry.ctx().get<Singletons::OrbitalCameraSettings>();
        if (!registry.valid(settings.entity))
            return;

        const f32 zoomDistance = Math::Clamp(DEFAULT_CAMERA_ZOOM_DISTANCE, 0.0f, CVAR_CameraZoomMaxDistance.GetFloat());
        CVAR_CameraZoomDistance.Set(zoomDistance);

        const vec3 defaultZoomOffset(0.0f, 1.8f, -zoomDistance);
        settings.cameraCurrentZoomOffset = defaultZoomOffset;
        settings.cameraTargetZoomOffset = defaultZoomOffset;
        settings.cameraZoomProgress = 1.0f;
        settings.cameraCollisionCurrentDistance = -1.0f;
        settings.cameraCollisionWasObstructed = false;

        auto& camera = registry.get<Components::Camera>(settings.entity);
        camera.pitch = DEFAULT_CAMERA_PITCH;
        camera.roll = 0.0f;
        camera.dirtyView = true;
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

        vec3 eulerAngles = vec3(glm::radians(camera.pitch), glm::radians(camera.yaw) + glm::pi<f32>(), glm::radians(camera.roll));

        f32 cameraHeightOffset = settings.cameraCurrentZoomOffset.y;
        f32 cameraZoomDistance = settings.cameraCurrentZoomOffset.z;

        // Update the camera
        vec3 resultPosition;
        quat resultRotation;
        Util::CameraUtil::CalculatePosRotForMatrix(characterControllerMatrix, eulerAngles, cameraHeightOffset, cameraZoomDistance, resultPosition, resultRotation);

        if (CVAR_CameraCollision.Get() != 0)
        {
            auto& joltState = ctx.get<Singletons::JoltState>();
            const vec3 focusPosition = vec3(characterControllerMatrix * vec4(0.0f, cameraHeightOffset, 0.0f, 1.0f));
            const vec3 focusToCamera = resultPosition - focusPosition;
            const f32 desiredDistanceSquared = glm::dot(focusToCamera, focusToCamera);
            if (desiredDistanceSquared > 0.000001f)
            {
                const f32 desiredDistance = glm::sqrt(desiredDistanceSquared);
                f32 targetDistance = desiredDistance;
                const bool hasCollision = TryGetCameraCollisionDistance(joltState, focusPosition, resultPosition, targetDistance);
                const bool isObstructed = hasCollision && targetDistance < desiredDistance - 0.001f;

                if (settings.cameraCollisionCurrentDistance < 0.0f)
                    settings.cameraCollisionCurrentDistance = desiredDistance;

                if (isObstructed)
                    settings.cameraCollisionWasObstructed = true;

                if (targetDistance < settings.cameraCollisionCurrentDistance)
                {
                    settings.cameraCollisionCurrentDistance = targetDistance;
                }
                else if (!settings.cameraCollisionWasObstructed)
                {
                    settings.cameraCollisionCurrentDistance = targetDistance;
                }
                else
                {
                    const f32 returnSpeed = glm::max(CVAR_CameraCollisionReturnSpeed.GetFloat(), 0.0f);
                    const f32 blend = returnSpeed > 0.0f ? 1.0f - glm::exp(-returnSpeed * deltaTime) : 1.0f;
                    settings.cameraCollisionCurrentDistance = glm::mix(settings.cameraCollisionCurrentDistance, targetDistance, blend);
                }

                const f32 minDistance = glm::max(CVAR_CameraCollisionMinDistance.GetFloat(), 0.0f);
                const f32 resolvedDistance = glm::clamp(settings.cameraCollisionCurrentDistance, minDistance, desiredDistance);
                resultPosition = focusPosition + (focusToCamera / desiredDistance) * resolvedDistance;

                if (!isObstructed && glm::abs(settings.cameraCollisionCurrentDistance - desiredDistance) <= 0.001f)
                {
                    settings.cameraCollisionCurrentDistance = desiredDistance;
                    settings.cameraCollisionWasObstructed = false;
                }
            }
        }
        else
        {
            settings.cameraCollisionCurrentDistance = -1.0f;
            settings.cameraCollisionWasObstructed = false;
        }

        vec3 resultRotationEuler = glm::eulerAngles(resultRotation);

        if (settings.mouseRightDown && characterSingleton.moverEntity != entt::null)
        {
            auto& movementInfo = registry.get<Components::MovementInfo>(characterSingleton.moverEntity);
            movementInfo.yaw = glm::radians(camera.yaw);

            if (!movementInfo.movementFlags.grounded && movementInfo.movementFlags.flying)
            {
                f32 pitch = glm::radians(-glm::clamp(camera.pitch, -45.0f, 89.0f));
                movementInfo.pitch = pitch;
            }
            else
            {
                movementInfo.pitch = 0.0f;
            }

            auto& unit = registry.get<Components::Unit>(characterSingleton.moverEntity);
            unit.positionOrRotationIsDirty = true;
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
            const f32 mouseSensitivity = ECS::Util::CameraUtil::GetMouseSensitivity();

            camera.yaw += -deltaPosition.x * mouseSensitivity;

            if (camera.yaw >= 360.0f)
                camera.yaw -= 360.0f;
            else if (camera.yaw < 0.0f)
                camera.yaw += 360.0f;

            camera.pitch = Math::Clamp(camera.pitch - (deltaPosition.y * mouseSensitivity), -89.0f, 89.0f);
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

        auto adjustSpeed = [scrollDelta = position.y](f32& speed)
        {
            speed += (speed / 20.0f) * scrollDelta;
            speed = glm::max(speed, 1.0f);
        };

        adjustSpeed(movementInfo.speeds.ground);
        adjustSpeed(movementInfo.speeds.flight);
        adjustSpeed(movementInfo.speeds.swim);
    }
}
