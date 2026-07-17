#include "CharacterController.h"
#include "CharacterControllerInput.h"

#include "Game-Lib/ECS/Components/AABB.h"
#include "Game-Lib/ECS/Components/AttachmentData.h"
#include "Game-Lib/ECS/Components/Camera.h"
#include "Game-Lib/ECS/Components/DisplayInfo.h"
#include "Game-Lib/ECS/Components/Events.h"
#include "Game-Lib/ECS/Components/Model.h"
#include "Game-Lib/ECS/Components/MovementInfo.h"
#include "Game-Lib/ECS/Components/Name.h"
#include "Game-Lib/ECS/Components/Tags.h"
#include "Game-Lib/ECS/Components/Unit.h"
#include "Game-Lib/ECS/Components/UnitCustomization.h"
#include "Game-Lib/ECS/Components/UnitEquipment.h"
#include "Game-Lib/ECS/Components/UnitPowersComponent.h"
#include "Game-Lib/ECS/Singletons/ActiveCamera.h"
#include "Game-Lib/ECS/Singletons/CharacterControllerSingleton.h"
#include "Game-Lib/ECS/Singletons/CharacterSingleton.h"
#include "Game-Lib/ECS/Singletons/FreeflyingCameraSettings.h"
#include "Game-Lib/ECS/Singletons/JoltState.h"
#include "Game-Lib/ECS/Singletons/NetworkState.h"
#include "Game-Lib/ECS/Singletons/OrbitalCameraSettings.h"
#include "Game-Lib/ECS/Util/CameraUtil.h"
#include "Game-Lib/ECS/Util/EventUtil.h"
#include "Game-Lib/ECS/Util/Transforms.h"
#include "Game-Lib/ECS/Util/Network/NetworkUtil.h"
#include "Game-Lib/Input/InputActionSystem.h"
#include "Game-Lib/Rendering/Debug/DebugRenderer.h"
#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Rendering/Model/ModelLoader.h"
#include "Game-Lib/Util/CharacterControllerUtil.h"
#include "Game-Lib/Util/ServiceLocator.h"
#include "Game-Lib/Util/UnitUtil.h"

#include <Base/CVarSystem/CVarSystem.h>
#include <Base/Math/Color.h>
#include <Base/Util/DebugHandler.h>

#include <MetaGen/Shared/Packet/Packet.h>

#include <Network/Client.h>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/ShapeFilter.h>

#include <entt/entt.hpp>
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>
#include <tracy/Tracy.hpp>

AutoCVar_Float CVAR_CharacterControllerShapeWidth(CVarCategory::Client | CVarCategory::Physics, "characterControllerShapeWidth", "Collision Width horizontal half-extent; requires controller reinitialization", 0.41666671634f, CVarFlags::EditFloatDrag);
AutoCVar_Float CVAR_CharacterControllerShapeHeight(CVarCategory::Client | CVarCategory::Physics, "characterControllerShapeHeight", "Collision Height apex-to-top height; requires controller reinitialization", 1.91345489025f, CVarFlags::EditFloatDrag);
AutoCVar_Float CVAR_CharacterControllerShapeConvexRadius(CVarCategory::Client | CVarCategory::Physics, "characterControllerShapeConvexRadius", "character controller hull convex radius; requires controller reinitialization", 0.05f, CVarFlags::EditFloatDrag);
AutoCVar_Float CVAR_CharacterControllerGravity(CVarCategory::Client | CVarCategory::Physics, "characterControllerGravity", "character controller gravity acceleration", -19.291105f, CVarFlags::EditFloatDrag);
AutoCVar_Float CVAR_CharacterControllerMaxSlopeAngle(CVarCategory::Client | CVarCategory::Physics, "characterControllerMaxSlopeAngle", "character controller max walkable slope in degrees", 50.0f, CVarFlags::EditFloatDrag);
AutoCVar_Float CVAR_CharacterControllerGroundSnapDistance(CVarCategory::Client | CVarCategory::Physics, "characterControllerGroundSnapDistance", "character controller grounded snap distance", 0.5f, CVarFlags::EditFloatDrag);
AutoCVar_Float CVAR_CharacterControllerGroundSnapGraceTime(CVarCategory::Client | CVarCategory::Physics, "characterControllerGroundSnapGraceTime", "character controller snap grace time after leaving ground", 0.1f, CVarFlags::EditFloatDrag);
AutoCVar_Float CVAR_CharacterControllerGroundSnapMaxDownVelocity(CVarCategory::Client | CVarCategory::Physics, "characterControllerGroundSnapMaxDownVelocity", "character controller max downward velocity that can snap to ground", 6.0f, CVarFlags::EditFloatDrag);
AutoCVar_Int CVAR_CharacterControllerMaxSubsteps(CVarCategory::Client | CVarCategory::Physics, "characterControllerMaxSubsteps", "fixed character controller physics substep count per fixed update", 4, CVarFlags::None);
AutoCVar_Float CVAR_CharacterControllerPredictiveContactDistance(CVarCategory::Client | CVarCategory::Physics, "characterControllerPredictiveContactDistance", "character controller predictive contact distance; requires controller reinitialization", 0.2f, CVarFlags::EditFloatDrag);
AutoCVar_Float CVAR_CharacterControllerFlightGroundProbeStartOffset(CVarCategory::Client | CVarCategory::Physics, "characterControllerFlightGroundProbeStartOffset", "character controller flight landing probe start offset above the foot", 0.25f, CVarFlags::EditFloatDrag);
AutoCVar_Float CVAR_CharacterControllerFlightGroundProbeDistance(CVarCategory::Client | CVarCategory::Physics, "characterControllerFlightGroundProbeDistance", "character controller flight landing probe distance below the start offset", 0.75f, CVarFlags::EditFloatDrag);
AutoCVar_Int CVAR_CharacterControllerEnhancedInternalEdgeRemoval(CVarCategory::Client | CVarCategory::Physics, "characterControllerEnhancedInternalEdgeRemoval", "enables Jolt enhanced internal edge removal for character controller", 0, CVarFlags::EditCheckbox);
AutoCVar_Float CVAR_CharacterControllerFlyGroundTransitionMinDownVelocity(CVarCategory::Client | CVarCategory::Physics, "characterControllerFlyGroundTransitionMinDownVelocity", "minimum downward velocity for character controller flying to grounded transition", 0.25f, CVarFlags::EditFloatDrag);
AutoCVar_Float CVAR_CharacterControllerWalkStairsStepUp(CVarCategory::Client | CVarCategory::Physics, "characterControllerWalkStairsStepUp", "character controller max stair step up distance", 1.1918f, CVarFlags::EditFloatDrag);
AutoCVar_Float CVAR_CharacterControllerWalkStairsMinStepForward(CVarCategory::Client | CVarCategory::Physics, "characterControllerWalkStairsMinStepForward", "character controller minimum stair step forward distance", 0.02f, CVarFlags::EditFloatDrag);
AutoCVar_Float CVAR_CharacterControllerWalkStairsStepForwardTest(CVarCategory::Client | CVarCategory::Physics, "characterControllerWalkStairsStepForwardTest", "character controller stair forward floor test distance", 0.1f, CVarFlags::EditFloatDrag);
AutoCVar_Float CVAR_CharacterControllerWalkStairsForwardContactAngle(CVarCategory::Client | CVarCategory::Physics, "characterControllerWalkStairsForwardContactAngle", "character controller stair forward contact angle in degrees", 50.0f, CVarFlags::EditFloatDrag);
AutoCVar_Int CVAR_CharacterControllerDebugDraw(CVarCategory::Client | CVarCategory::Physics, "characterControllerDebugDraw", "draws character controller debug vectors", 0, CVarFlags::EditCheckbox);
AutoCVar_Float CVAR_CharacterControllerDebugVelocityScale(CVarCategory::Client | CVarCategory::Physics, "characterControllerDebugVelocityScale", "character controller debug velocity vector scale", 0.25f, CVarFlags::EditFloatDrag);

namespace
{
    namespace CharacterControllerUtil = ::Util::CharacterController;

    struct CharacterMotorUpdateContext
    {
    public:
        ECS::Singletons::CharacterControllerSingleton& state;
        const ECS::Singletons::CharacterControllerSettings& settings;
        ECS::Components::MovementInfo& movementInfo;
        const quat& characterRotation;
        const JPH::Vec3& currentVelocity;
        bool isGrounded;
        bool hasMovementInput;
    };

    struct CharacterMotorUpdateResult
    {
    public:
        JPH::Vec3 moveDirection;
        JPH::Vec3 persistentVelocity;
        JPH::Vec3 solveVelocity;
        f32 speed;
        bool justStartedJump = false;
    };

    struct CharacterMotorSensingResult
    {
    public:
        bool isGrounded = false;
    };

    struct CharacterMotorPreUpdateContext
    {
    public:
        const ECS::Singletons::CharacterControllerSingleton& state;
        const ECS::Components::MovementInfo& movementInfo;
        const CharacterMotorSensingResult& sensing;
    };

    struct CharacterMotorPostUpdateContext
    {
    public:
        ECS::Singletons::CharacterControllerSingleton& state;
        const ECS::Singletons::CharacterControllerSettings& settings;
        ECS::Singletons::JoltState& joltState;
        ECS::Components::MovementInfo& movementInfo;
        ECS::TransformSystem& transformSystem;
        CharacterMotorUpdateResult& motorResult;
        JPH::Vec3& persistentVelocity;
        f32 flyGroundTransitionMinDownVelocity;
    };

    static_assert(sizeof(CharacterMotorUpdateContext) <= 64);
    static_assert(sizeof(CharacterMotorUpdateResult) <= 64);
    static_assert(sizeof(CharacterMotorSensingResult) <= 64);
    static_assert(sizeof(CharacterMotorPreUpdateContext) <= 64);
    static_assert(sizeof(CharacterMotorPostUpdateContext) <= 64);

    enum class CharacterMotorTransitionPhase : u8
    {
        PreUpdate,
        PostUpdate
    };

    void InitInput(ECS::Singletons::CharacterControllerSingleton& state)
    {
        InputActionSystem* inputActions = ServiceLocator::GetInputActionSystem();
        state.inputContext = inputActions->CreateContext("CharacterGameplay", GameInputPriority::Gameplay);
        state.cameraInputContext = inputActions->CreateContext("CharacterCamera", GameInputPriority::Camera);
        inputActions->SetContextActive(state.inputContext, false);
        inputActions->SetContextActive(state.cameraInputContext, true);

        const auto cancelAutorun = [](const InputActionEvent& event)
        {
            if (event.phase == InputPhase::Pressed)
            {
                entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
                auto& state = registry->ctx().get<ECS::Singletons::CharacterControllerSingleton>();
                state.autorunEnabled = false;
            }

            return InputReply::Consumed;
        };

        state.moveForwardAction = inputActions->RegisterAction(state.inputContext, "MoveForward", "Move Forward", "Movement", InputBinding::Keyboard(Key::W, InputModifier::None, ModifierMatch::Any), cancelAutorun);
        state.moveBackwardAction = inputActions->RegisterAction(state.inputContext, "MoveBackward", "Move Backward", "Movement", InputBinding::Keyboard(Key::S, InputModifier::None, ModifierMatch::Any), cancelAutorun);
        state.strafeLeftAction = inputActions->RegisterAction(state.inputContext, "StrafeLeft", "Strafe Left", "Movement", InputBinding::Keyboard(Key::A, InputModifier::None, ModifierMatch::Any));
        state.strafeRightAction = inputActions->RegisterAction(state.inputContext, "StrafeRight", "Strafe Right", "Movement", InputBinding::Keyboard(Key::D, InputModifier::None, ModifierMatch::Any));
        state.jumpAction = inputActions->RegisterAction(state.inputContext, "Jump", "Jump", "Movement", InputBinding::Keyboard(Key::Space, InputModifier::None, ModifierMatch::Any));

        inputActions->RegisterAction(state.inputContext, "ToggleAutorun", "Toggle Autorun", "Movement",
            InputBinding::Mouse(MouseButton::Middle, InputModifier::None, ModifierMatch::Any),
            { .defaultReply = InputReply::Handled }, [](const InputActionEvent& event)
        {
            if (event.phase != InputPhase::Pressed)
                return InputReply::Handled;

            entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
            auto& state = registry->ctx().get<ECS::Singletons::CharacterControllerSingleton>();
            if (!state.controlMask.allowForcedForward)
                return InputReply::Ignored;

            state.autorunEnabled = !state.autorunEnabled;
            return InputReply::Consumed;
        });

        inputActions->RegisterAction(state.inputContext, "SelectTarget", "Select Target", "Targeting",
            InputBinding::Mouse(MouseButton::Left, InputModifier::None, ModifierMatch::Any),
            { .defaultReply = InputReply::Handled }, ECS::Systems::CharacterControllerInput::HandleTargetInput);

        inputActions->RegisterAction(state.inputContext, "InteractTarget", "Interact With Target", "Targeting",
            InputBinding::Mouse(MouseButton::Right, InputModifier::None, ModifierMatch::Any),
            { .defaultReply = InputReply::Handled }, ECS::Systems::CharacterControllerInput::HandleTargetInput);

        inputActions->RegisterAction(state.cameraInputContext, "ToggleCameraMode", "Toggle Camera Mode", "Camera",
            InputBinding::Keyboard(Key::C, InputModifier::None, ModifierMatch::Any),
            { .defaultReply = InputReply::Handled }, [](const InputActionEvent& event)
        {
            if (event.phase != InputPhase::Pressed)
                return InputReply::Handled;

            entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
            entt::registry::context& ctx = registry->ctx();

            auto& activeCamera = ctx.get<ECS::Singletons::ActiveCamera>();
            auto& state = ctx.get<ECS::Singletons::CharacterControllerSingleton>();
            auto& freeFlyingCameraSettings = ctx.get<ECS::Singletons::FreeflyingCameraSettings>();
            auto& orbitalCameraSettings = ctx.get<ECS::Singletons::OrbitalCameraSettings>();

            InputActionSystem* inputActions = ServiceLocator::GetInputActionSystem();
            const InputActionContextHandle freeFlyingCameraContext = inputActions->GetContext("FreeFlyingCamera"_x);
            const InputActionContextHandle orbitalCameraContext = inputActions->GetContext("OrbitalCamera"_x);

            if (activeCamera.entity == orbitalCameraSettings.entity)
            {
                if (!registry->valid(freeFlyingCameraSettings.entity))
                    return InputReply::Ignored;

                activeCamera.entity = freeFlyingCameraSettings.entity;

                inputActions->SetContextActive(state.inputContext, false);
                inputActions->SetContextActive(orbitalCameraContext, false);
                inputActions->SetContextActive(freeFlyingCameraContext, true);
                ECS::Util::CameraUtil::SetCaptureMouse(false);
            }
            else if (activeCamera.entity == freeFlyingCameraSettings.entity)
            {
                if (!registry->valid(orbitalCameraSettings.entity))
                    return InputReply::Ignored;

                activeCamera.entity = orbitalCameraSettings.entity;

                inputActions->SetContextActive(freeFlyingCameraContext, false);
                inputActions->SetContextActive(state.inputContext, true);
                inputActions->SetContextActive(orbitalCameraContext, true);
                ECS::Util::CameraUtil::SetCaptureMouse(false);
            }
            else
            {
                return InputReply::Ignored;
            }

            ECS::Components::MovementInfo* movementInfo = nullptr;
            if (registry->valid(state.moverEntity))
                movementInfo = registry->try_get<ECS::Components::MovementInfo>(state.moverEntity);

            CharacterControllerUtil::ResetMovementInput(state, movementInfo);

            auto& camera = registry->get<ECS::Components::Camera>(activeCamera.entity);
            camera.dirtyView = true;
            camera.dirtyPerspective = true;
            return InputReply::Consumed;
        });

        inputActions->RegisterAction(state.cameraInputContext, "ReleaseFreeCameraMouseCapture", "Release Free Camera Mouse Capture", "Camera",
            InputBinding::Keyboard(Key::Escape, InputModifier::None, ModifierMatch::Any),
            { .defaultReply = InputReply::Ignored, .rebindable = false }, [](const InputActionEvent& event)
        {
            if (event.phase != InputPhase::Pressed)
                return InputReply::Ignored;

            entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
            auto& ctx = registry->ctx();
            const auto& activeCamera = ctx.get<ECS::Singletons::ActiveCamera>();
            const auto& settings = ctx.get<ECS::Singletons::FreeflyingCameraSettings>();
            if (activeCamera.entity != settings.entity || !settings.captureMouse)
                return InputReply::Ignored;

            ECS::Util::CameraUtil::SetCaptureMouse(false);
            return InputReply::Consumed;
        });

        inputActions->RegisterAction(state.cameraInputContext, "MoveCharacterToCamera", "Move Character To Camera", "Debug",
            InputBinding::Keyboard(Key::G, InputModifier::None, ModifierMatch::Any),
            { .defaultReply = InputReply::Handled, .rebindable = false }, [](const InputActionEvent& event)
        {
            if (event.phase != InputPhase::Pressed)
                return InputReply::Handled;

            entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
            entt::registry::context& ctx = registry->ctx();

            auto& state = ctx.get<ECS::Singletons::CharacterControllerSingleton>();
            auto& activeCamera = ctx.get<ECS::Singletons::ActiveCamera>();
            auto& freeFlyingCameraSettings = ctx.get<ECS::Singletons::FreeflyingCameraSettings>();
            if (activeCamera.entity != freeFlyingCameraSettings.entity || !registry->valid(state.moverEntity))
                return InputReply::Ignored;

            auto& transformSystem = ctx.get<ECS::TransformSystem>();
            auto& camera = registry->get<ECS::Components::Camera>(freeFlyingCameraSettings.entity);
            auto& cameraTransform = registry->get<ECS::Components::Transform>(freeFlyingCameraSettings.entity);
            auto& movementInfo = registry->get<ECS::Components::MovementInfo>(state.moverEntity);
            auto& unit = registry->get<ECS::Components::Unit>(state.moverEntity);

            const vec3 newPosition = cameraTransform.GetWorldPosition();
            if (state.character)
            {
                state.character->SetLinearVelocity(JPH::Vec3::sZero());
                state.character->SetPosition(JPH::RVec3Arg(newPosition.x, newPosition.y, newPosition.z));
            }
            transformSystem.SetWorldPosition(state.controllerEntity, newPosition);

            movementInfo.pitch = 0.0f;
            movementInfo.yaw = glm::radians(camera.yaw);
            unit.positionOrRotationIsDirty = true;
            return InputReply::Consumed;
        });
    }

    ECS::Singletons::CharacterControllerSingleton& GetOrCreateState(entt::registry& registry)
    {
        entt::registry::context& ctx = registry.ctx();
        if (!ctx.contains<ECS::Singletons::CharacterControllerSingleton>())
            return ctx.emplace<ECS::Singletons::CharacterControllerSingleton>();

        return ctx.get<ECS::Singletons::CharacterControllerSingleton>();
    }

    ECS::Singletons::CharacterControllerSettings& GetOrCreateSettings(entt::registry& registry)
    {
        entt::registry::context& ctx = registry.ctx();
        if (!ctx.contains<ECS::Singletons::CharacterControllerSettings>())
            return ctx.emplace<ECS::Singletons::CharacterControllerSettings>();

        return ctx.get<ECS::Singletons::CharacterControllerSettings>();
    }

    void ApplyRuntimeSettingsFromCVars(ECS::Singletons::CharacterControllerSettings& settings)
    {
        settings.gravity = CVAR_CharacterControllerGravity.GetFloat();
        settings.maxWalkableSlopeAngleRadians = glm::radians(CVAR_CharacterControllerMaxSlopeAngle.GetFloat());
        settings.groundSnapDistance = glm::max(0.0f, CVAR_CharacterControllerGroundSnapDistance.GetFloat());
        settings.groundSnapGraceTime = glm::max(0.0f, CVAR_CharacterControllerGroundSnapGraceTime.GetFloat());
        settings.groundSnapMaxDownVelocity = glm::max(0.0f, CVAR_CharacterControllerGroundSnapMaxDownVelocity.GetFloat());
        settings.maxSubsteps = static_cast<u8>(glm::clamp(CVAR_CharacterControllerMaxSubsteps.Get(), 1, 4));
        settings.flightGroundProbeStartOffset = glm::max(0.0f, CVAR_CharacterControllerFlightGroundProbeStartOffset.GetFloat());
        settings.flightGroundProbeDistance = glm::max(0.0f, CVAR_CharacterControllerFlightGroundProbeDistance.GetFloat());
        settings.walkStairsStepUp = glm::max(0.0f, CVAR_CharacterControllerWalkStairsStepUp.GetFloat());
        settings.walkStairsMinStepForward = glm::max(0.0f, CVAR_CharacterControllerWalkStairsMinStepForward.GetFloat());
        settings.walkStairsStepForwardTest = glm::max(0.0f, CVAR_CharacterControllerWalkStairsStepForwardTest.GetFloat());
        settings.walkStairsForwardContactAngleRadians = glm::radians(glm::clamp(CVAR_CharacterControllerWalkStairsForwardContactAngle.GetFloat(), 0.0f, 180.0f));
        settings.up = ECS::Components::Transform::WORLD_UP;
        settings.enhancedInternalEdgeRemoval = CVAR_CharacterControllerEnhancedInternalEdgeRemoval.Get() != 0;
    }

    ECS::Singletons::CharacterControllerSettings BuildSettingsFromCVars()
    {
        ECS::Singletons::CharacterControllerSettings settings;
        settings.collisionHalfWidth = glm::max(1.0e-3f, CVAR_CharacterControllerShapeWidth.GetFloat());
        settings.collisionHeight = glm::max(1.0e-3f, CVAR_CharacterControllerShapeHeight.GetFloat());
        settings.shapeConvexRadius = glm::max(0.0f, CVAR_CharacterControllerShapeConvexRadius.GetFloat());
        ApplyRuntimeSettingsFromCVars(settings);
        settings.predictiveContactDistance = glm::max(0.01f, CVAR_CharacterControllerPredictiveContactDistance.GetFloat());

        return settings;
    }

    ECS::Singletons::CharacterMovementIntent BuildIntent(entt::registry& registry, ECS::Singletons::CharacterControllerSingleton& state, bool isAlive)
    {
        ECS::Singletons::CharacterMovementIntent intent;
        InputActionSystem* inputActions = ServiceLocator::GetInputActionSystem();
        if (!isAlive || !inputActions->IsContextActive(state.inputContext))
            return intent;

        entt::registry::context& ctx = registry.ctx();
        ECS::Singletons::OrbitalCameraSettings* orbitalCameraSettings = ctx.find<ECS::Singletons::OrbitalCameraSettings>();

        if (state.controlMask.allowForwardBack)
        {
            const bool moveForward = inputActions->IsDown(state.moveForwardAction);
            const bool moveBackward = inputActions->IsDown(state.moveBackwardAction);
            intent.mouseForward = orbitalCameraSettings && orbitalCameraSettings->mouseLeftDown && orbitalCameraSettings->mouseRightDown;

            const bool mouseForwardStarted = intent.mouseForward && !state.intent.mouseForward;
            if (mouseForwardStarted)
                state.autorunEnabled = false;

            intent.moveForward = moveForward || intent.mouseForward;
            intent.moveBackward = moveBackward;
        }

        intent.autorun = state.controlMask.allowForcedForward && state.autorunEnabled;

        if (state.controlMask.allowStrafe)
        {
            intent.strafeLeft = inputActions->IsDown(state.strafeLeftAction);
            intent.strafeRight = inputActions->IsDown(state.strafeRightAction);
        }

        if (state.controlMask.allowJump || state.controlMask.allowAscendDescend)
            intent.jumpOrAscend = inputActions->IsDown(state.jumpAction);

        if (state.controlMask.allowAscendDescend)
            intent.descend = state.intent.descend;

        return intent;
    }

    CharacterMotorSensingResult SenseCharacterMotor(const ECS::Singletons::CharacterControllerSingleton& state, const ECS::Singletons::CharacterControllerSettings& settings, const ECS::Components::MovementInfo& movementInfo, const JPH::Vec3Arg& currentVelocity)
    {
        CharacterMotorSensingResult result;
        const bool isFlying = state.activeMotor == ECS::Singletons::CharacterMotorType::Flight;
        const bool isJumping = movementInfo.movementFlags.jumping || movementInfo.jumpState != ECS::Components::JumpState::None;
        const JPH::Vec3 up = CharacterControllerUtil::ToJolt(settings.up);
        const bool isRisingJump = !isFlying && (isJumping || state.preserveSteepSlopeJumpVelocityUntilFalling) && currentVelocity.Dot(up) > 1.0e-3f;
        result.isGrounded = !isRisingJump && CharacterControllerUtil::IsOnGround(state.character);
        return result;
    }

    namespace GroundMotor
    {
        bool CanTransitionToFlight(const ECS::Singletons::CharacterControllerSingleton& state, const ECS::Components::MovementInfo& movementInfo, bool isGrounded)
        {
            if (!movementInfo.movementFlags.flying || !state.controlMask.allowModeTransitions)
                return false;

            if (!isGrounded)
                return true;

            return state.intent.jumpOrAscend && state.controlMask.allowAscendDescend;
        }

        ECS::Singletons::CharacterMotorType PreUpdate(const CharacterMotorPreUpdateContext& context)
        {
            if (CanTransitionToFlight(context.state, context.movementInfo, context.sensing.isGrounded))
                return ECS::Singletons::CharacterMotorType::Flight;

            return ECS::Singletons::CharacterMotorType::Ground;
        }

        void Enter(ECS::Singletons::CharacterControllerSingleton& state, ECS::Components::MovementInfo& movementInfo, CharacterMotorTransitionPhase transitionPhase)
        {
            if (transitionPhase != CharacterMotorTransitionPhase::PostUpdate)
                return;

            state.preserveSteepSlopeJumpVelocityUntilFalling = false;
            state.neutralJumpAirControlAvailable = false;
            state.neutralJumpAirControlConsumed = false;

            movementInfo.movementFlags.jumping = false;
            movementInfo.jumpState = ECS::Components::JumpState::None;
        }

        void Exit(ECS::Singletons::CharacterControllerSingleton& state, ECS::Components::MovementInfo& movementInfo)
        {
            (void)state;
            (void)movementInfo;
        }

        CharacterMotorUpdateResult Update(const CharacterMotorUpdateContext& context)
        {
            CharacterMotorUpdateResult result;
            result.moveDirection = CharacterControllerUtil::BuildCharacterRelativeMoveDirection(context.state.intent, context.characterRotation);
            result.speed = CharacterControllerUtil::GetPlanarSpeed(context.movementInfo, context.state.intent, ECS::Singletons::CharacterMotorType::Ground);
            result.persistentVelocity = context.currentVelocity;
            result.solveVelocity = context.currentVelocity;

            if (context.isGrounded)
            {
                const JPH::Vec3 groundVelocity = context.state.character->GetGroundVelocity();
                result.persistentVelocity = groundVelocity + result.moveDirection * result.speed;
                result.solveVelocity = result.persistentVelocity;
            }
            else
            {
                if (context.state.neutralJumpAirControlAvailable && !context.state.neutralJumpAirControlConsumed && context.hasMovementInput && !result.moveDirection.IsNearZero())
                {
                    const f32 airControlSpeed = result.speed * context.settings.neutralJumpAirControlMultiplier;
                    result.persistentVelocity.SetX(result.moveDirection.GetX() * airControlSpeed);
                    result.persistentVelocity.SetZ(result.moveDirection.GetZ() * airControlSpeed);
                    context.state.neutralJumpAirControlConsumed = true;
                }

                result.solveVelocity = result.persistentVelocity;
            }

            const bool canJump = context.isGrounded && context.state.controlMask.allowJump && !context.movementInfo.movementFlags.jumping && context.movementInfo.jumpState == ECS::Components::JumpState::None;
            if (context.state.intent.jumpOrAscend && canJump)
            {
                const f32 jumpSpeed = context.movementInfo.jumpSpeed * context.movementInfo.gravityModifier;
                result.persistentVelocity.SetY(jumpSpeed);
                result.solveVelocity.SetY(jumpSpeed);
                context.movementInfo.movementFlags.jumping = true;
                context.movementInfo.jumpState = ECS::Components::JumpState::Begin;
                result.justStartedJump = true;

                context.state.neutralJumpAirControlAvailable = !context.hasMovementInput;
                context.state.neutralJumpAirControlConsumed = false;
                context.state.preserveSteepSlopeJumpVelocityUntilFalling = true;
            }

            return result;
        }
    }

    namespace FlightMotor
    {
        ECS::Singletons::CharacterMotorType PreUpdate(const CharacterMotorPreUpdateContext& context)
        {
            if (!context.movementInfo.movementFlags.flying)
                return ECS::Singletons::CharacterMotorType::Ground;

            return ECS::Singletons::CharacterMotorType::Flight;
        }

        bool CanTransitionToGround(const ECS::Singletons::CharacterControllerSingleton& state, const ECS::Singletons::CharacterControllerSettings& settings, const JPH::Vec3Arg& velocity, f32 flyGroundTransitionMinDownVelocity)
        {
            if (!state.controlMask.allowModeTransitions || state.intent.jumpOrAscend)
                return false;

            const JPH::Vec3 up = CharacterControllerUtil::ToJolt(settings.up);
            const f32 minDownVelocity = glm::max(0.0f, flyGroundTransitionMinDownVelocity);
            return velocity.Dot(up) <= -minDownVelocity;
        }

        void Enter(ECS::Singletons::CharacterControllerSingleton& state, ECS::Components::MovementInfo& movementInfo)
        {
            state.preserveSteepSlopeJumpVelocityUntilFalling = false;
            state.neutralJumpAirControlAvailable = false;
            state.neutralJumpAirControlConsumed = false;
            state.groundSnapGraceTimer = 0.0f;

            movementInfo.movementFlags.jumping = false;
            movementInfo.jumpState = ECS::Components::JumpState::None;
        }

        void Exit(ECS::Singletons::CharacterControllerSingleton& state, ECS::Components::MovementInfo& movementInfo)
        {
            (void)state;
            (void)movementInfo;
        }

        CharacterMotorUpdateResult Update(const CharacterMotorUpdateContext& context)
        {
            CharacterMotorUpdateResult result;
            result.moveDirection = CharacterControllerUtil::BuildCharacterRelativeMoveDirection(context.state.intent, context.characterRotation);
            const bool ascend = context.state.intent.jumpOrAscend && context.state.controlMask.allowAscendDescend;
            const bool descend = context.state.intent.descend && context.state.controlMask.allowAscendDescend;
            if (ascend != descend)
            {
                result.moveDirection += CharacterControllerUtil::ToJolt(context.settings.up) * (ascend ? 1.0f : -1.0f);
                result.moveDirection = result.moveDirection.Normalized();
            }

            result.speed = CharacterControllerUtil::GetPlanarSpeed(context.movementInfo, context.state.intent, ECS::Singletons::CharacterMotorType::Flight);
            result.persistentVelocity = result.moveDirection * result.speed;
            result.solveVelocity = result.persistentVelocity;

            return result;
        }

        ECS::Singletons::CharacterMotorType PostUpdate(const CharacterMotorPostUpdateContext& context, ECS::Singletons::CharacterControllerDebugSingleton* debugState)
        {
            if (!CanTransitionToGround(context.state, context.settings, context.persistentVelocity, context.flyGroundTransitionMinDownVelocity))
                return ECS::Singletons::CharacterMotorType::Flight;

            JPH::Vec3 groundNormal;
            JPH::Vec3 groundPosition;
            const bool foundGroundTransition = CharacterControllerUtil::TryGetWalkableGroundProbeNormal(context.state.character, context.settings, context.joltState, context.state.character->GetPosition(), context.settings.flightGroundProbeStartOffset, context.settings.flightGroundProbeDistance, groundNormal, &groundPosition, debugState);
            if (!foundGroundTransition)
                return ECS::Singletons::CharacterMotorType::Flight;

            context.state.character->SetPosition(JPH::RVec3Arg(groundPosition.GetX(), groundPosition.GetY(), groundPosition.GetZ()));

            const quat groundRotation = quat(vec3(0.0f, context.movementInfo.yaw, 0.0f));
            context.transformSystem.SetWorldRotation(context.state.moverEntity, groundRotation);
            context.state.character->SetRotation(JPH::Quat(groundRotation.x, groundRotation.y, groundRotation.z, groundRotation.w));
            context.state.appliedPitch = 0.0f;
            context.movementInfo.pitch = 0.0f;

            context.motorResult.moveDirection = CharacterControllerUtil::BuildCharacterRelativeMoveDirection(context.state.intent, groundRotation);
            const f32 groundSpeed = CharacterControllerUtil::GetPlanarSpeed(context.movementInfo, context.state.intent, ECS::Singletons::CharacterMotorType::Ground);
            context.persistentVelocity = context.state.character->GetGroundVelocity() + context.motorResult.moveDirection * groundSpeed;
            context.state.character->SetLinearVelocity(context.persistentVelocity);

            context.motorResult.speed = groundSpeed;
            context.motorResult.persistentVelocity = context.persistentVelocity;
            context.motorResult.solveVelocity = context.persistentVelocity;
            context.state.groundSnapGraceTimer = context.settings.groundSnapGraceTime;

            return ECS::Singletons::CharacterMotorType::Ground;
        }
    }

    ECS::Singletons::CharacterMotorType PreUpdateMotor(ECS::Singletons::CharacterMotorType motor, const CharacterMotorPreUpdateContext& context)
    {
        switch (motor)
        {
            case ECS::Singletons::CharacterMotorType::Flight:
                return FlightMotor::PreUpdate(context);
            case ECS::Singletons::CharacterMotorType::Ground:
            default:
                return GroundMotor::PreUpdate(context);
        }
    }

    ECS::Singletons::CharacterMotorType PostUpdateMotor(ECS::Singletons::CharacterMotorType motor, const CharacterMotorPostUpdateContext& context, ECS::Singletons::CharacterControllerDebugSingleton* debugState)
    {
        switch (motor)
        {
            case ECS::Singletons::CharacterMotorType::Flight:
                return FlightMotor::PostUpdate(context, debugState);
            case ECS::Singletons::CharacterMotorType::Ground:
            default:
                return ECS::Singletons::CharacterMotorType::Ground;
        }
    }

    CharacterMotorUpdateResult UpdateMotor(ECS::Singletons::CharacterMotorType motor, const CharacterMotorUpdateContext& context)
    {
        switch (motor)
        {
            case ECS::Singletons::CharacterMotorType::Flight:
                return FlightMotor::Update(context);
            case ECS::Singletons::CharacterMotorType::Ground:
            default:
                return GroundMotor::Update(context);
        }
    }

    void TransitionMotor(ECS::Singletons::CharacterControllerSingleton& state, ECS::Components::MovementInfo& movementInfo, ECS::Singletons::CharacterMotorType nextMotor, CharacterMotorTransitionPhase transitionPhase)
    {
        const ECS::Singletons::CharacterMotorType previousMotor = state.activeMotor;
        if (previousMotor == nextMotor)
            return;

        switch (previousMotor)
        {
            case ECS::Singletons::CharacterMotorType::Flight:
                FlightMotor::Exit(state, movementInfo);
                break;
            case ECS::Singletons::CharacterMotorType::Ground:
            default:
                GroundMotor::Exit(state, movementInfo);
                break;
        }

        state.activeMotor = nextMotor;
        switch (nextMotor)
        {
            case ECS::Singletons::CharacterMotorType::Flight:
                FlightMotor::Enter(state, movementInfo);
                break;
            case ECS::Singletons::CharacterMotorType::Ground:
            default:
                GroundMotor::Enter(state, movementInfo, transitionPhase);
                break;
        }
    }

    u8 GetPhysicsSubstepCount(const ECS::Singletons::CharacterControllerSettings& settings)
    {
        return static_cast<u8>(glm::clamp(static_cast<i32>(settings.maxSubsteps), 1, 4));
    }

    void TryWalkStairs(
        JPH::CharacterVirtual* character,
        const ECS::Singletons::CharacterControllerSettings& settings,
        const JPH::Vec3Arg& desiredVelocity,
        JPH::RVec3Arg substepStartPosition,
        f32 substepDeltaTime,
        const JPH::BroadPhaseLayerFilter& broadPhaseLayerFilter,
        const JPH::ObjectLayerFilter& objectLayerFilter,
        const JPH::BodyFilter& bodyFilter,
        const JPH::ShapeFilter& shapeFilter,
        JPH::TempAllocator& allocator)
    {
        if (!character || settings.walkStairsStepUp <= 0.0f || substepDeltaTime <= 0.0f)
            return;

        const JPH::Vec3 up = CharacterControllerUtil::ToJolt(settings.up);
        JPH::Vec3 desiredHorizontalStep = desiredVelocity * substepDeltaTime;
        desiredHorizontalStep -= desiredHorizontalStep.Dot(up) * up;
        const f32 desiredHorizontalStepLength = desiredHorizontalStep.Length();
        if (desiredHorizontalStepLength <= 1.0e-6f)
            return;

        JPH::Vec3 achievedHorizontalStep = JPH::Vec3(character->GetPosition() - substepStartPosition);
        achievedHorizontalStep -= achievedHorizontalStep.Dot(up) * up;

        const JPH::Vec3 stepForwardNormalized = desiredHorizontalStep / desiredHorizontalStepLength;
        const f32 achievedForwardLength = glm::max(0.0f, achievedHorizontalStep.Dot(stepForwardNormalized));
        if (achievedForwardLength + 1.0e-4f >= desiredHorizontalStepLength || !character->CanWalkStairs(desiredVelocity))
            return;

        JPH::Vec3 walkStairsDirection = stepForwardNormalized;
        f32 maximumContactDot = glm::cos(settings.walkStairsForwardContactAngleRadians);
        for (const JPH::CharacterContact& contact : character->GetActiveContacts())
        {
            if (!contact.mHadCollision
                || contact.mWasDiscarded
                || contact.mSurfaceNormal.Dot(desiredVelocity - contact.mLinearVelocity) >= 0.0f
                || !character->IsSlopeTooSteep(contact.mSurfaceNormal))
            {
                continue;
            }

            JPH::Vec3 contactDirection = contact.mSurfaceNormal.Dot(up) * up - contact.mSurfaceNormal;
            const f32 contactDirectionLength = contactDirection.Length();
            if (contactDirectionLength <= 1.0e-6f)
                continue;

            contactDirection /= contactDirectionLength;
            const f32 contactDot = contactDirection.Dot(stepForwardNormalized);
            if (contactDot <= maximumContactDot)
                continue;

            walkStairsDirection = contactDirection;
            maximumContactDot = contactDot;
        }

        const f32 missingForwardDistance = desiredHorizontalStepLength - achievedForwardLength;
        const JPH::Vec3 stepForward = walkStairsDirection * glm::max(settings.walkStairsMinStepForward, missingForwardDistance);
        const JPH::Vec3 stepForwardTest = walkStairsDirection * settings.walkStairsStepForwardTest;
        character->WalkStairs(
            substepDeltaTime,
            up * settings.walkStairsStepUp,
            stepForward,
            stepForwardTest,
            JPH::Vec3::sZero(),
            broadPhaseLayerFilter,
            objectLayerFilter,
            bodyFilter,
            shapeFilter,
            allocator);
    }

    ECS::Singletons::CharacterControllerGroundDebugState GetGroundDebugState(JPH::CharacterVirtual::EGroundState groundState)
    {
        switch (groundState)
        {
            case JPH::CharacterVirtual::EGroundState::OnGround:
                return ECS::Singletons::CharacterControllerGroundDebugState::OnGround;
            case JPH::CharacterVirtual::EGroundState::OnSteepGround:
                return ECS::Singletons::CharacterControllerGroundDebugState::OnSteepGround;
            case JPH::CharacterVirtual::EGroundState::NotSupported:
                return ECS::Singletons::CharacterControllerGroundDebugState::NotSupported;
            case JPH::CharacterVirtual::EGroundState::InAir:
            default:
                return ECS::Singletons::CharacterControllerGroundDebugState::InAir;
        }
    }

    Color GetGroundDebugColor(ECS::Singletons::CharacterControllerGroundDebugState groundState)
    {
        switch (groundState)
        {
            case ECS::Singletons::CharacterControllerGroundDebugState::OnGround:
                return Color::Green;

            case ECS::Singletons::CharacterControllerGroundDebugState::OnSteepGround:
                return Color::PastelOrange;

            case ECS::Singletons::CharacterControllerGroundDebugState::NotSupported:
                return Color(0.0f, 1.0f, 1.0f, 1.0f);

            case ECS::Singletons::CharacterControllerGroundDebugState::InAir:
            default:
                return Color::Red;
        }
    }

    void DrawDebugVector(DebugRenderer* debugRenderer, const vec3& origin, const vec3& vector, Color color, f32 scale)
    {
        if (!debugRenderer || scale <= 0.0f || glm::dot(vector, vector) <= 1.0e-6f)
            return;

        debugRenderer->DrawLine3D(origin, origin + vector * scale, color);
    }

    void DrawDebugCharacterShape(DebugRenderer* debugRenderer, const ECS::Singletons::CharacterControllerSingleton& state, const ECS::Singletons::CharacterControllerSettings& settings)
    {
        if (!debugRenderer || !state.character)
            return;

        const CharacterControllerUtil::BoxPyramidShapeDimensions dimensions = CharacterControllerUtil::GetBoxPyramidShapeDimensions(settings);
        const f32 halfWidth = dimensions.collisionHalfWidth;
        const f32 pyramidHeight = dimensions.pyramidHeight;
        const f32 top = dimensions.collisionHeight;
        const JPH::Vec3 localPoints[9] =
        {
            {-halfWidth, top, -halfWidth},
            {-halfWidth, top,  halfWidth},
            { halfWidth, top,  halfWidth},
            { halfWidth, top, -halfWidth},
            {-halfWidth, pyramidHeight, -halfWidth},
            {-halfWidth, pyramidHeight,  halfWidth},
            { halfWidth, pyramidHeight,  halfWidth},
            { halfWidth, pyramidHeight, -halfWidth},
            { 0.0f, 0.0f, 0.0f }
        };

        static constexpr u8 edges[][2] =
        {
            {0, 1}, {1, 2}, {2, 3}, {3, 0},
            {4, 5}, {5, 6}, {6, 7}, {7, 4},
            {0, 4}, {1, 5}, {2, 6}, {3, 7},
            {4, 8}, {5, 8}, {6, 8}, {7, 8}
        };

        const JPH::Vec3 position = JPH::Vec3(state.character->GetPosition());
        const JPH::Vec3 paddingOffset = CharacterControllerUtil::ToJolt(settings.up) * state.character->GetCharacterPadding();
        const JPH::Quat rotation = state.character->GetRotation();
        vec3 worldPoints[9];
        for (u8 i = 0; i < 9; i++)
            worldPoints[i] = CharacterControllerUtil::FromJolt(position + paddingOffset + rotation * localPoints[i]);

        for (const auto& edge : edges)
            debugRenderer->DrawLine3D(worldPoints[edge[0]], worldPoints[edge[1]], Color::Cyan);
    }

    void DrawDebugCharacterContacts(DebugRenderer* debugRenderer, const ECS::Singletons::CharacterControllerSingleton& state, const ECS::Singletons::CharacterControllerSettings& settings)
    {
        if (!debugRenderer || !state.character)
            return;

        for (const JPH::CharacterContact& contact : state.character->GetActiveContacts())
        {
            const vec3 position(
                static_cast<f32>(contact.mPosition.GetX()),
                static_cast<f32>(contact.mPosition.GetY()),
                static_cast<f32>(contact.mPosition.GetZ()));

            if (contact.mWasDiscarded)
            {
                debugRenderer->DrawSphere3D(position, 0.025f, 6, Color::Red);
                continue;
            }

            const bool isWalkable = CharacterControllerUtil::IsWalkableGroundNormal(state.character, settings, contact.mSurfaceNormal);
            const Color surfaceNormalColor = isWalkable ? Color::Green : Color::PastelOrange;
            const Color contactColor = contact.mHadCollision ? Color::White : Color::Gray;
            const Color contactNormalColor = contact.mHadCollision ? Color::Cyan : Color::Gray;
            const f32 contactRadius = contact.mHadCollision ? 0.025f : 0.015f;

            debugRenderer->DrawSphere3D(position, contactRadius, 6, contactColor);
            DrawDebugVector(debugRenderer, position, CharacterControllerUtil::FromJolt(contact.mSurfaceNormal), surfaceNormalColor, 0.35f);
            DrawDebugVector(debugRenderer, position, CharacterControllerUtil::FromJolt(contact.mContactNormal), contactNormalColor, 0.25f);
        }
    }

    void CaptureDebugMovement(
        ECS::Singletons::CharacterControllerDebugSingleton& debugState,
        const ECS::Singletons::CharacterControllerSingleton& state,
        const ECS::Singletons::CharacterControllerSettings& settings,
        const JPH::Vec3Arg& position,
        const JPH::Vec3Arg& moveDirection,
        const JPH::Vec3Arg& persistentVelocity,
        const JPH::Vec3Arg& solveVelocity,
        const JPH::Vec3Arg& actualVelocity,
        const JPH::Vec3Arg& movementGroundNormal,
        f32 speed,
        bool isEffectivelyGrounded,
        bool supportProbeUsedForGrounding,
        bool shouldSnapToGround)
    {
        if (!state.character)
        {
            debugState.valid = false;
            return;
        }

        debugState.position = ::Util::CharacterController::FromJolt(position);
        debugState.inputVelocity = ::Util::CharacterController::FromJolt(moveDirection * speed);
        debugState.persistentVelocity = ::Util::CharacterController::FromJolt(persistentVelocity);
        debugState.solveVelocity = ::Util::CharacterController::FromJolt(solveVelocity);
        debugState.actualVelocity = ::Util::CharacterController::FromJolt(actualVelocity);
        debugState.groundNormal = ::Util::CharacterController::FromJolt(state.character->GetGroundNormal());
        debugState.movementGroundNormal = ::Util::CharacterController::FromJolt(movementGroundNormal);
        debugState.snapStepDown = shouldSnapToGround ? CharacterControllerUtil::FromJolt(CharacterControllerUtil::GetGroundSnapStepDown(settings)) : vec3(0.0f);
        debugState.groundState = isEffectivelyGrounded ? ECS::Singletons::CharacterControllerGroundDebugState::OnGround : GetGroundDebugState(state.character->GetGroundState());
        debugState.groundSnapGraceTimer = state.groundSnapGraceTimer;
        debugState.supportProbeUsedForGrounding = supportProbeUsedForGrounding;
        debugState.valid = true;
    }

    void DrawDebugMovement(
        const ECS::Singletons::CharacterControllerDebugSingleton& debugState,
        const ECS::Singletons::CharacterControllerSingleton& state,
        const ECS::Singletons::CharacterControllerSettings& settings)
    {
        if (!debugState.valid)
            return;

        GameRenderer* gameRenderer = ServiceLocator::GetGameRenderer();
        DebugRenderer* debugRenderer = gameRenderer ? gameRenderer->GetDebugRenderer() : nullptr;
        if (!debugRenderer)
            return;

        const vec3 up = settings.up;
        const vec3 origin = debugState.position + up * 0.15f;
        const f32 velocityScale = glm::max(0.0f, CVAR_CharacterControllerDebugVelocityScale.GetFloat());

        DrawDebugCharacterShape(debugRenderer, state, settings);
        DrawDebugCharacterContacts(debugRenderer, state, settings);

        debugRenderer->DrawSphere3D(origin, 0.05f, 8, GetGroundDebugColor(debugState.groundState));
        DrawDebugVector(debugRenderer, origin, debugState.inputVelocity, Color::Red, velocityScale);
        DrawDebugVector(debugRenderer, origin + up * 0.05f, debugState.persistentVelocity, Color::Green, velocityScale);
        DrawDebugVector(debugRenderer, origin + up * 0.1f, debugState.solveVelocity, Color::Blue, velocityScale);
        DrawDebugVector(debugRenderer, origin + up * 0.15f, debugState.actualVelocity, Color::White, velocityScale);
        DrawDebugVector(debugRenderer, origin, debugState.groundNormal, Color::Magenta, 0.75f);
        DrawDebugVector(debugRenderer, origin + up * 0.05f, debugState.movementGroundNormal, Color::Gray, 0.75f);
        DrawDebugVector(debugRenderer, origin, debugState.snapStepDown, Color::Yellow, 1.0f);

        if (debugState.supportProbeActive)
        {
            Color supportProbeColor = Color::Red;
            if (debugState.supportProbeHit && !debugState.supportProbeWalkable)
                supportProbeColor = Color::PastelOrange;
            else if (debugState.supportProbeWalkable)
                supportProbeColor = Color(0.0f, 1.0f, 1.0f, 1.0f);

            if (debugState.supportProbeUsedForGrounding)
                supportProbeColor = Color::Green;

            debugRenderer->DrawLine3D(debugState.supportProbeStart, debugState.supportProbeEnd, supportProbeColor);
            if (debugState.supportProbeHit)
            {
                debugRenderer->DrawSphere3D(debugState.supportProbeHitPosition, 0.035f, 8, supportProbeColor);
                DrawDebugVector(debugRenderer, debugState.supportProbeHitPosition, debugState.supportProbeNormal, supportProbeColor, 0.5f);
            }
        }

        if (debugState.groundSnapGraceTimer > 0.0f && debugState.groundState != ECS::Singletons::CharacterControllerGroundDebugState::OnGround)
            debugRenderer->DrawLine3D(origin - up * 0.1f, origin + up * 0.1f, Color(0.0f, 1.0f, 1.0f, 1.0f));
    }

} // namespace

namespace ECS::Systems
{
    void CharacterController::Init(entt::registry& registry)
    {
        auto& state = GetOrCreateState(registry);
        entt::registry::context& ctx = registry.ctx();
        if (!ctx.contains<Singletons::CharacterControllerDebugSingleton>())
            ctx.emplace<Singletons::CharacterControllerDebugSingleton>();

        if (state.initialized)
            return;

        auto& transformSystem = ctx.get<TransformSystem>();
        auto& characterSingleton = ctx.emplace<Singletons::CharacterSingleton>();

        state.controllerEntity = registry.create();
        characterSingleton.controllerEntity = state.controllerEntity;

        auto& name = registry.emplace<Components::Name>(state.controllerEntity);
        name.fullName = "CharacterController";
        name.name = "CharacterController";
        name.nameHash = StringUtils::fnv1a_32(name.name.c_str(), name.name.size());
        registry.emplace<Components::Transform>(state.controllerEntity);
        transformSystem.SetWorldPosition(state.controllerEntity, vec3(0.0f));

        InitCharacterController(registry, true);
        InitInput(state);
    }

    void CharacterController::Update(entt::registry& registry, f32)
    {
        ZoneScopedN("ECS::CharacterController");

        entt::registry::context& ctx = registry.ctx();
        auto& state = ctx.get<Singletons::CharacterControllerSingleton>();
        auto& networkState = ctx.get<Singletons::NetworkState>();

        Util::EventUtil::OnEvent<Components::MapLoadedEvent>([&](const Components::MapLoadedEvent&)
        {
            const bool isLocal = !networkState.client || !networkState.client->IsConnected();
            InitCharacterController(registry, isLocal);
        });

        if (!state.initialized || !state.character || !ServiceLocator::GetInputActionSystem()->IsContextActive(state.inputContext))
            return;

        auto& settings = ctx.get<Singletons::CharacterControllerSettings>();
        auto& joltState = ctx.get<Singletons::JoltState>();
        static constexpr f32 fixedDeltaTime = Singletons::JoltState::FixedDeltaTime;
        const bool debugDrawEnabled = CVAR_CharacterControllerDebugDraw.Get() != 0;
        if (joltState.updateTimer < fixedDeltaTime)
        {
            if (debugDrawEnabled)
                DrawDebugMovement(ctx.get<Singletons::CharacterControllerDebugSingleton>(), state, settings);

            return;
        }

        auto& transformSystem = ctx.get<TransformSystem>();
        auto& movementInfo = registry.get<Components::MovementInfo>(state.moverEntity);
        auto& unit = registry.get<Components::Unit>(state.moverEntity);
        auto& unitPowers = registry.get<Components::UnitPowersComponent>(state.moverEntity);
        const auto& healthPower = ::Util::Unit::GetPower(unitPowers, MetaGen::Shared::Unit::PowerTypeEnum::Health);
        const bool isAlive = healthPower.current > 0.0f;

        ApplyRuntimeSettingsFromCVars(settings);
        const f32 flyGroundTransitionMinDownVelocity = CVAR_CharacterControllerFlyGroundTransitionMinDownVelocity.GetFloat();

        Singletons::CharacterControllerDebugSingleton* debugState = nullptr;
        if (debugDrawEnabled)
        {
            debugState = &ctx.get<Singletons::CharacterControllerDebugSingleton>();
            CharacterControllerUtil::ResetGroundProbeDebug(*debugState);
        }

        state.character->SetMaxSlopeAngle(settings.maxWalkableSlopeAngleRadians);
        state.character->SetEnhancedInternalEdgeRemoval(settings.enhancedInternalEdgeRemoval);

        const Singletons::CharacterMovementIntent previousIntent = state.intent;
        if (!isAlive)
            CharacterControllerUtil::ResetMovementInput(state, &movementInfo);

        state.intent = BuildIntent(registry, state, isAlive);

        const bool wasFlying = state.activeMotor == Singletons::CharacterMotorType::Flight;
        const bool wasGrounded = movementInfo.movementFlags.grounded;
        const JPH::Vec3 currentVelocity = state.character->GetLinearVelocity();
        const JPH::Vec3 joltUp = ::Util::CharacterController::ToJolt(settings.up);

        CharacterMotorSensingResult sensing = SenseCharacterMotor(state, settings, movementInfo, currentVelocity);
        CharacterMotorPreUpdateContext preUpdateContext =
        {
            .state = state,
            .movementInfo = movementInfo,
            .sensing = sensing
        };

        const Singletons::CharacterMotorType preUpdateMotor = state.activeMotor;
        Singletons::CharacterMotorType nextMotor = PreUpdateMotor(state.activeMotor, preUpdateContext);
        TransitionMotor(state, movementInfo, nextMotor, CharacterMotorTransitionPhase::PreUpdate);
        if (state.activeMotor != preUpdateMotor)
        {
            if (debugState)
                CharacterControllerUtil::ResetGroundProbeDebug(*debugState);

            sensing = SenseCharacterMotor(state, settings, movementInfo, currentVelocity);
        }

        bool isFlying = state.activeMotor == Singletons::CharacterMotorType::Flight;
        bool isGrounded = sensing.isGrounded;
        const bool hasMovementInput = CharacterControllerUtil::HasMovementInput(state.intent);
        CharacterControllerUtil::UpdateGroundSnapGrace(state, settings, isGrounded, fixedDeltaTime);

        if (state.controlMask.allowYaw)
            state.appliedYaw = movementInfo.yaw;
        else
            movementInfo.yaw = state.appliedYaw;

        if (!isFlying)
        {
            state.appliedPitch = 0.0f;
            movementInfo.pitch = 0.0f;
        }
        else if (state.controlMask.allowPitch)
        {
            state.appliedPitch = movementInfo.pitch;
        }
        else
        {
            movementInfo.pitch = state.appliedPitch;
        }

        const quat characterRotation = quat(vec3(state.appliedPitch, state.appliedYaw, 0.0f));
        const JPH::Quat joltRotation(characterRotation.x, characterRotation.y, characterRotation.z, characterRotation.w);

        if (isAlive)
        {
            transformSystem.SetWorldRotation(state.moverEntity, characterRotation);
            state.character->SetRotation(joltRotation);
        }

        const CharacterMotorUpdateContext motorContext =
        {
            .state = state,
            .settings = settings,
            .movementInfo = movementInfo,
            .characterRotation = characterRotation,
            .currentVelocity = currentVelocity,
            .isGrounded = isGrounded,
            .hasMovementInput = hasMovementInput
        };

        CharacterMotorUpdateResult motorResult = UpdateMotor(state.activeMotor, motorContext);

        JPH::Vec3& moveDirection = motorResult.moveDirection;
        JPH::Vec3& persistentVelocity = motorResult.persistentVelocity;
        JPH::Vec3& solveVelocity = motorResult.solveVelocity;
        f32& speed = motorResult.speed;
        const bool justStartedJump = motorResult.justStartedJump;
        JPH::Vec3 movementGroundNormal = state.character->GetGroundNormal();

        JPH::DefaultBroadPhaseLayerFilter broadPhaseLayerFilter(joltState.objectVSBroadPhaseLayerFilter, Jolt::Layers::MOVING);
        JPH::DefaultObjectLayerFilter objectLayerFilter(joltState.objectVSObjectLayerFilter, Jolt::Layers::MOVING);
        JPH::BodyFilter bodyFilter;
        JPH::ShapeFilter shapeFilter;
        JPH::Vec3 gravity = isFlying ? JPH::Vec3::sZero() : JPH::Vec3(0.0f, settings.gravity, 0.0f);
        const JPH::RVec3 positionBeforeUpdate = state.character->GetPosition();
        const u8 substepCount = GetPhysicsSubstepCount(settings);
        const f32 substepDeltaTime = fixedDeltaTime / static_cast<f32>(substepCount);
        bool groundMotorActiveForTick = !isFlying && isGrounded && !justStartedJump;
        JPH::Vec3 groundVelocityForTick = groundMotorActiveForTick ? state.character->GetGroundVelocity() : JPH::Vec3::sZero();
        if (groundMotorActiveForTick)
            movementGroundNormal = CharacterControllerUtil::ResolveGroundMovementNormal(state.character, settings);

        bool skipMotionUpdate = false;
        const bool canRefreshIdleContacts = groundMotorActiveForTick
            && !hasMovementInput
            && persistentVelocity.IsNearZero()
            && groundVelocityForTick.IsNearZero();
        if (canRefreshIdleContacts)
        {
            state.character->SetLinearVelocity(JPH::Vec3::sZero());
            state.character->RefreshContacts(broadPhaseLayerFilter, objectLayerFilter, bodyFilter, shapeFilter, joltState.allocator);

            bool hasExternalContactVelocity = false;
            for (const JPH::CharacterContact& contact : state.character->GetActiveContacts())
            {
                if (contact.mHadCollision
                    && !contact.mWasDiscarded
                    && contact.mCanPushCharacter
                    && !contact.mLinearVelocity.IsNearZero())
                {
                    hasExternalContactVelocity = true;
                    break;
                }
            }

            skipMotionUpdate = CharacterControllerUtil::IsOnGround(state.character) && !hasExternalContactVelocity;
            if (!skipMotionUpdate && !CharacterControllerUtil::IsOnGround(state.character))
                groundMotorActiveForTick = false;
        }

        bool snappedToGround = false;

        for (u8 substepIndex = 0; !skipMotionUpdate && substepIndex < substepCount; substepIndex++)
        {
            const JPH::RVec3 substepStartPosition = state.character->GetPosition();
            const bool startedJumpThisSubstep = justStartedJump && substepIndex == 0;
            const bool isJumpingBeforeSubstep = movementInfo.movementFlags.jumping || movementInfo.jumpState != Components::JumpState::None;

            if (groundMotorActiveForTick)
            {
                const bool hasGroundContactThisSubstep = CharacterControllerUtil::IsOnGround(state.character);
                if (hasGroundContactThisSubstep)
                {
                    groundVelocityForTick = state.character->GetGroundVelocity();
                    movementGroundNormal = CharacterControllerUtil::ResolveGroundMovementNormal(state.character, settings);
                }

                const JPH::Vec3 planarVelocity = moveDirection * speed;
                persistentVelocity = groundVelocityForTick + planarVelocity;
                const JPH::Vec3 groundSlopeVelocity = hasGroundContactThisSubstep
                    ? CharacterControllerUtil::BuildGroundSlopeVelocity(state.character, settings, planarVelocity, movementGroundNormal)
                    : JPH::Vec3::sZero();
                // Stable walkable support does not accelerate the character downward.
                solveVelocity = persistentVelocity + groundSlopeVelocity;
            }
            else
            {
                if (!isFlying && !startedJumpThisSubstep)
                    persistentVelocity += joltUp * (settings.gravity * movementInfo.gravityModifier * substepDeltaTime);

                solveVelocity = persistentVelocity;
            }

            const bool shouldPreserveSteepSlopeJumpVelocity = CharacterControllerUtil::ShouldPreserveSteepSlopeJumpVelocity(state, settings, persistentVelocity, isFlying, isJumpingBeforeSubstep, startedJumpThisSubstep);
            if (substepIndex == 0 && !shouldPreserveSteepSlopeJumpVelocity)
                solveVelocity = state.character->CancelVelocityTowardsSteepSlopes(solveVelocity);

            state.character->SetLinearVelocity(solveVelocity);
            state.character->Update(substepDeltaTime, gravity, broadPhaseLayerFilter, objectLayerFilter, bodyFilter, shapeFilter, joltState.allocator);

            if (!isFlying && !isJumpingBeforeSubstep)
                TryWalkStairs(state.character, settings, persistentVelocity, substepStartPosition, substepDeltaTime, broadPhaseLayerFilter, objectLayerFilter, bodyFilter, shapeFilter, joltState.allocator);

            state.character->SetLinearVelocity(persistentVelocity);

            const bool isJumpingForHeadContact = isJumpingBeforeSubstep || state.preserveSteepSlopeJumpVelocityUntilFalling;
            if (CharacterControllerUtil::HasRisingHeadContact(state, settings, persistentVelocity, isFlying, isJumpingForHeadContact))
            {
                persistentVelocity = CharacterControllerUtil::RemoveUpwardVelocity(settings, persistentVelocity);
                state.character->SetLinearVelocity(persistentVelocity);
                state.preserveSteepSlopeJumpVelocityUntilFalling = false;
            }

            const f32 verticalSpeedAfterSubstep = persistentVelocity.Dot(joltUp);
            if (state.preserveSteepSlopeJumpVelocityUntilFalling && (isFlying || verticalSpeedAfterSubstep <= 1.0e-3f))
                state.preserveSteepSlopeJumpVelocityUntilFalling = false;
        }

        const bool shouldSnapToGround = CharacterControllerUtil::ShouldSnapToGround(state, settings, persistentVelocity, isFlying, justStartedJump);
        if (shouldSnapToGround && !CharacterControllerUtil::IsOnGround(state.character))
        {
            JPH::Vec3 snapGroundNormal;
            const f32 pyramidHeight = CharacterControllerUtil::GetBoxPyramidShapeDimensions(settings).pyramidHeight;
            const bool foundGroundSnapCandidate = CharacterControllerUtil::TryGetWalkableGroundProbeNormal(state.character, settings, joltState, state.character->GetPosition(), pyramidHeight, settings.groundSnapDistance, snapGroundNormal, nullptr, debugState);
            if (foundGroundSnapCandidate)
            {
                snappedToGround = state.character->StickToFloor(CharacterControllerUtil::GetGroundSnapStepDown(settings), broadPhaseLayerFilter, objectLayerFilter, bodyFilter, shapeFilter, joltState.allocator)
                    && CharacterControllerUtil::IsOnGround(state.character);
            }
        }

        const CharacterMotorPostUpdateContext postUpdateContext =
        {
            .state = state,
            .settings = settings,
            .joltState = joltState,
            .movementInfo = movementInfo,
            .transformSystem = transformSystem,
            .motorResult = motorResult,
            .persistentVelocity = persistentVelocity,
            .flyGroundTransitionMinDownVelocity = flyGroundTransitionMinDownVelocity
        };

        const Singletons::CharacterMotorType postUpdateMotor = state.activeMotor;
        nextMotor = PostUpdateMotor(postUpdateMotor, postUpdateContext, debugState);
        const bool transitionedFromFlyToGround = postUpdateMotor == Singletons::CharacterMotorType::Flight && nextMotor == Singletons::CharacterMotorType::Ground;
        TransitionMotor(state, movementInfo, nextMotor, CharacterMotorTransitionPhase::PostUpdate);
        isFlying = state.activeMotor == Singletons::CharacterMotorType::Flight;

        const f32 verticalSpeedAfterUpdate = persistentVelocity.Dot(joltUp);
        if (state.preserveSteepSlopeJumpVelocityUntilFalling && (isFlying || verticalSpeedAfterUpdate <= 1.0e-3f))
            state.preserveSteepSlopeJumpVelocityUntilFalling = false;

        const bool isRisingJump = !isFlying && (movementInfo.movementFlags.jumping || state.preserveSteepSlopeJumpVelocityUntilFalling) && verticalSpeedAfterUpdate > 1.0e-3f;
        isGrounded = !isFlying && !isRisingJump && (CharacterControllerUtil::IsOnGround(state.character) || transitionedFromFlyToGround);
        if (isGrounded)
        {
            const f32 groundVerticalSpeed = state.character->GetGroundVelocity().Dot(joltUp);
            persistentVelocity += joltUp * (groundVerticalSpeed - persistentVelocity.Dot(joltUp));
            state.character->SetLinearVelocity(persistentVelocity);
            movementGroundNormal = CharacterControllerUtil::ResolveGroundMovementNormal(state.character, settings);
        }

        const JPH::Vec3 position = state.character->GetPosition();
        const JPH::Vec3 actualVelocity = JPH::Vec3(state.character->GetPosition() - positionBeforeUpdate) / fixedDeltaTime;

        transformSystem.SetWorldPosition(state.controllerEntity, vec3(position.GetX(), position.GetY(), position.GetZ()));

        movementInfo.horizontalVelocity = vec2(actualVelocity.GetX(), actualVelocity.GetZ());
        movementInfo.verticalVelocity = actualVelocity.GetY();

        if (isGrounded)
            state.groundSnapGraceTimer = settings.groundSnapGraceTime;

        if (debugState)
        {
            CaptureDebugMovement(*debugState, state, settings, position, moveDirection, persistentVelocity, solveVelocity, actualVelocity, movementGroundNormal, speed, isGrounded, snappedToGround || transitionedFromFlyToGround, snappedToGround);
            DrawDebugMovement(*debugState, state, settings);
        }

        const bool moveForward = state.intent.moveForward || state.intent.autorun || state.intent.mouseForward;
        movementInfo.movementFlags.forward = moveForward && !state.intent.moveBackward;
        movementInfo.movementFlags.backward = state.intent.moveBackward && !moveForward;
        movementInfo.movementFlags.left = state.intent.strafeLeft && !state.intent.strafeRight;
        movementInfo.movementFlags.right = state.intent.strafeRight && !state.intent.strafeLeft;
        movementInfo.movementFlags.grounded = isGrounded;
        movementInfo.movementFlags.justGrounded = !wasGrounded && isGrounded;
        movementInfo.movementFlags.justEndedJump = false;

        if (isGrounded)
        {
            state.neutralJumpAirControlAvailable = false;
            state.neutralJumpAirControlConsumed = false;
            state.preserveSteepSlopeJumpVelocityUntilFalling = false;

            if (movementInfo.movementFlags.jumping || movementInfo.jumpState != Components::JumpState::None)
            {
                movementInfo.movementFlags.jumping = false;
                movementInfo.movementFlags.justEndedJump = true;
                movementInfo.jumpState = Components::JumpState::None;
            }
        }

        const bool movementIntentChanged = previousIntent != state.intent;
        unit.positionOrRotationIsDirty = unit.positionOrRotationIsDirty || movementIntentChanged || wasGrounded != isGrounded || wasFlying != isFlying || !isGrounded || hasMovementInput;

        const bool hadMovementInput = CharacterControllerUtil::HasMovementInput(previousIntent);
        if (!isGrounded || hasMovementInput || hadMovementInput || unit.positionOrRotationIsDirty)
        {
            if (networkState.client && networkState.client->IsConnected())
            {
                Util::Network::SendPacket(networkState, MetaGen::Shared::Packet::ClientUnitMovePacket{
                    .movementFlags = *reinterpret_cast<u32*>(&movementInfo.movementFlags),
                    .position = vec3(position.GetX(), position.GetY(), position.GetZ()),
                    .pitchYaw = vec2(movementInfo.pitch, movementInfo.yaw),
                    .verticalVelocity = movementInfo.verticalVelocity
                });
            }

            unit.positionOrRotationIsDirty = false;
        }
    }

    void CharacterController::InitCharacterController(entt::registry& registry, bool isLocal)
    {
        entt::registry::context& ctx = registry.ctx();
        auto& state = ctx.get<Singletons::CharacterControllerSingleton>();
        auto& settings = GetOrCreateSettings(registry);
        auto& characterSingleton = ctx.get<Singletons::CharacterSingleton>();
        auto& transformSystem = ctx.get<TransformSystem>();

        if (isLocal)
        {
            if (state.moverEntity == entt::null)
                state.moverEntity = registry.create();

            characterSingleton.moverEntity = state.moverEntity;
            NC_ASSERT(registry.valid(state.moverEntity), "CharacterController mover entity is leaking");

            auto& name = registry.get_or_emplace<Components::Name>(state.moverEntity);
            auto& aabb = registry.get_or_emplace<Components::AABB>(state.moverEntity);
            registry.emplace_or_replace<Components::WorldAABB>(state.moverEntity);
            auto& transform = registry.get_or_emplace<Components::Transform>(state.moverEntity);
            auto& moverModel = registry.get_or_emplace<Components::Model>(state.moverEntity);
            auto& movementInfo = registry.get_or_emplace<Components::MovementInfo>(state.moverEntity);
            auto& attachment = registry.get_or_emplace<Components::AttachmentData>(state.moverEntity);

            auto& unit = registry.get_or_emplace<Components::Unit>(state.moverEntity);
            unit.name = "Localplayer";
            unit.targetEntity = entt::null;
            unit.unitClass = GameDefine::UnitClass::Warrior;
            unit.race = GameDefine::UnitRace::Human;
            unit.gender = GameDefine::UnitGender::Female;

            auto& unitEquipment = registry.get_or_emplace<Components::UnitEquipment>(state.moverEntity);
            for (u32 i = 0; i <= static_cast<u32>(MetaGen::Shared::Unit::ItemEquipSlotEnum::EquipmentEnd); i++)
            {
                auto equipSlot = static_cast<MetaGen::Shared::Unit::ItemEquipSlotEnum>(i);
                unitEquipment.dirtyItemIDSlots.insert(equipSlot);
            }
            registry.emplace_or_replace<Components::UnitEquipmentDirty>(state.moverEntity);

            auto& displayInfo = registry.get_or_emplace<Components::DisplayInfo>(state.moverEntity);
            displayInfo.displayID = 50;

            auto& unitCustomization = registry.get_or_emplace<Components::UnitCustomization>(state.moverEntity);
            auto& unitPowers = registry.get_or_emplace<Components::UnitPowersComponent>(state.moverEntity);
            ::Util::Unit::AddPower(unitPowers, MetaGen::Shared::Unit::PowerTypeEnum::Health, 100.0, 50.0, 100.0);

            name.fullName = "Localplayer";
            name.name = "Localplayer";
            name.nameHash = StringUtils::fnv1a_32(name.name.c_str(), name.name.size());

            transformSystem.SetWorldPosition(state.moverEntity, vec3(0.0f));
            ModelLoader* modelLoader = ServiceLocator::GetGameRenderer()->GetModelLoader();
            modelLoader->LoadDisplayIDForEntity(state.moverEntity, moverModel, Database::Unit::DisplayInfoType::Creature, 50);
            registry.emplace_or_replace<Components::PlayerTag>(state.moverEntity);
        }
        else
        {
            state.moverEntity = characterSingleton.moverEntity;
        }

        if (!registry.valid(state.controllerEntity) || !registry.valid(state.moverEntity))
        {
            NC_LOG_ERROR("CharacterController failed to initialize because its controller or mover entity is invalid");
            return;
        }

        settings = BuildSettingsFromCVars();

        const JPH::ShapeRefC shape = ::Util::CharacterController::CreateBoxPyramidShape(settings);
        if (!shape)
        {
            NC_LOG_ERROR("CharacterController failed to create its collision shape");
            return;
        }

        const JPH::Vec3 up = ::Util::CharacterController::ToJolt(settings.up);

        JPH::CharacterVirtualSettings characterSettings;
        characterSettings.mShape = shape;
        characterSettings.mUp = up;
        characterSettings.mBackFaceMode = JPH::EBackFaceMode::IgnoreBackFaces;
        characterSettings.mPredictiveContactDistance = settings.predictiveContactDistance;
        characterSettings.mPenetrationRecoverySpeed = settings.penetrationRecoverySpeed;
        characterSettings.mMaxSlopeAngle = settings.maxWalkableSlopeAngleRadians;
        characterSettings.mEnhancedInternalEdgeRemoval = settings.enhancedInternalEdgeRemoval;

        auto& joltState = ctx.get<Singletons::JoltState>();
        auto& transform = registry.get<Components::Transform>(state.moverEntity);
        const vec3 newPosition = transform.GetWorldPosition();

        if (state.character)
            delete state.character;

        state.character = new JPH::CharacterVirtual(&characterSettings, JPH::RVec3(newPosition.x, newPosition.y, newPosition.z), JPH::Quat::sIdentity(), &joltState.physicsSystem);
        state.character->SetUp(up);
        state.character->SetShapeOffset(JPH::Vec3::sZero());
        state.character->SetMass(settings.characterMass);
        state.character->SetLinearVelocity(JPH::Vec3::sZero());

        characterSingleton.character = state.character;
        characterSingleton.controllerEntity = state.controllerEntity;
        characterSingleton.moverEntity = state.moverEntity;

        auto& movementInfo = registry.get<Components::MovementInfo>(state.moverEntity);
        movementInfo.gravityModifier = 1.0f;
        movementInfo.movementFlags.jumping = false;
        movementInfo.movementFlags.justGrounded = false;
        movementInfo.movementFlags.justEndedJump = false;
        movementInfo.jumpState = Components::JumpState::None;

        state.intent = {};
        state.activeMotor = Singletons::CharacterMotorType::Ground;
        state.groundSnapGraceTimer = 0.0f;
        state.appliedPitch = movementInfo.pitch;
        state.appliedYaw = movementInfo.yaw;
        state.neutralJumpAirControlAvailable = false;
        state.neutralJumpAirControlConsumed = false;
        state.preserveSteepSlopeJumpVelocityUntilFalling = false;

        auto& unit = registry.get<Components::Unit>(state.moverEntity);
        unit.positionOrRotationIsDirty = true;

        registry.emplace_or_replace<Components::LocalPlayerTag>(state.moverEntity);

        auto& orbitalCameraSettings = ctx.get<Singletons::OrbitalCameraSettings>();
        auto& camera = registry.get<Components::Camera>(orbitalCameraSettings.entity);
        camera.yaw = glm::degrees(movementInfo.yaw);

        transformSystem.ParentEntityTo(state.controllerEntity, state.moverEntity);
        transformSystem.SetLocalPosition(state.moverEntity, vec3(0.0f));
        transformSystem.SetWorldPosition(state.controllerEntity, newPosition);

        ctx.get<Singletons::CharacterControllerDebugSingleton>() = Singletons::CharacterControllerDebugSingleton();
        state.initialized = true;
    }

    void CharacterController::DeleteCharacterController(entt::registry& registry, bool isLocal)
    {
        entt::registry::context& ctx = registry.ctx();
        auto* state = ctx.find<Singletons::CharacterControllerSingleton>();
        if (!state)
            return;

        auto& characterSingleton = ctx.get<Singletons::CharacterSingleton>();
        if (characterSingleton.character == state->character)
            characterSingleton.character = nullptr;

        if (state->character)
        {
            delete state->character;
            state->character = nullptr;
        }

        if (state->moverEntity != entt::null)
        {
            if (isLocal && registry.valid(state->moverEntity))
            {
                if (auto* model = registry.try_get<Components::Model>(state->moverEntity))
                {
                    ModelLoader* modelLoader = ServiceLocator::GetGameRenderer()->GetModelLoader();
                    modelLoader->UnloadModelForEntity(state->moverEntity, *model);
                }

                auto& transformSystem = ctx.get<TransformSystem>();
                transformSystem.ClearParent(state->moverEntity);
                registry.destroy(state->moverEntity);
            }

            state->moverEntity = entt::null;
            characterSingleton.moverEntity = entt::null;
        }

        if (auto* debugState = ctx.find<Singletons::CharacterControllerDebugSingleton>())
            debugState->valid = false;

        CharacterControllerUtil::ResetMovementInput(*state, nullptr);
        state->activeMotor = Singletons::CharacterMotorType::Ground;
        state->groundSnapGraceTimer = 0.0f;
        state->appliedPitch = 0.0f;
        state->appliedYaw = 0.0f;
        state->neutralJumpAirControlAvailable = false;
        state->neutralJumpAirControlConsumed = false;
        state->preserveSteepSlopeJumpVelocityUntilFalling = false;
        state->initialized = false;
    }
}
