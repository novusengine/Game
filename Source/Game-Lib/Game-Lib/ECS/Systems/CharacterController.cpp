#include "CharacterController.h"

#include "Game/Animation/AnimationSystem.h"
#include "Game/ECS/Components/AABB.h"
#include "Game/ECS/Components/Camera.h"
#include "Game/ECS/Components/DisplayInfo.h"
#include "Game/ECS/Components/Events.h"
#include "Game/ECS/Components/Model.h"
#include "Game/ECS/Components/MovementInfo.h"
#include "Game/ECS/Components/Name.h"
#include "Game/ECS/Components/NetworkedEntity.h"
#include "Game/ECS/Components/UnitStatsComponent.h"
#include "Game/ECS/Singletons/ActiveCamera.h"
#include "Game/ECS/Singletons/CharacterSingleton.h"
#include "Game/ECS/Singletons/FreeflyingCameraSettings.h"
#include "Game/ECS/Singletons/JoltState.h"
#include "Game/ECS/Singletons/NetworkState.h"
#include "Game/ECS/Singletons/OrbitalCameraSettings.h"
#include "Game/ECS/Util/EventUtil.h"
#include "Game/ECS/Util/MessageBuilderUtil.h"
#include "Game/ECS/Util/Transforms.h"
#include "Game/Editor/EditorHandler.h"
#include "Game/Gameplay/MapLoader.h"
#include "Game/Rendering/GameRenderer.h"
#include "Game/Rendering/Debug/DebugRenderer.h"
#include "Game/Rendering/Debug/JoltDebugRenderer.h"
#include "Game/Rendering/Model/ModelLoader.h"
#include "Game/Util/CharacterControllerUtil.h"
#include "Game/Util/PhysicsUtil.h"
#include "Game/Util/UnitUtil.h"
#include "Game/Util/ServiceLocator.h"

#include <Gameplay/Network/Opcode.h>

#include <Input/InputManager.h>
#include <Input/KeybindGroup.h>

#include <Network/Client.h>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>

#include <entt/entt.hpp>
#include <GLFW/glfw3.h>
#include <Game/ECS/Util/CameraUtil.h>
#include <Base/CVarSystem/CVarSystem.h>

#include <tracy/Tracy.hpp>

#define USE_CHARACTER_CONTROLLER_V2 0

namespace ECS::Systems
{
    void CharacterController::Init(entt::registry& registry)
    {
        entt::registry::context& ctx = registry.ctx();

        auto& joltState = ctx.get<Singletons::JoltState>();
        auto& transformSystem = ctx.get<TransformSystem>();
        auto& characterSingleton = ctx.emplace<Singletons::CharacterSingleton>();

        characterSingleton.controllerEntity = registry.create();
        auto& name = registry.emplace<Components::Name>(characterSingleton.controllerEntity);
        name.fullName = "CharacterController";
        name.name = "CharacterController";
        name.nameHash = StringUtils::fnv1a_32(name.name.c_str(), name.name.size());
        registry.emplace<Components::Transform>(characterSingleton.controllerEntity);

        transformSystem.SetWorldPosition(characterSingleton.controllerEntity, vec3(0.0f, 0.0f, 0.0f));

        InitCharacterController(registry, true);

        InputManager* inputManager = ServiceLocator::GetInputManager();
        characterSingleton.keybindGroup = inputManager->CreateKeybindGroup("CharacterController", 100);
        characterSingleton.cameraToggleKeybindGroup = inputManager->CreateKeybindGroup("CharacterController - Camera Toggle", 1000);
        characterSingleton.keybindGroup->SetActive(false);

        characterSingleton.keybindGroup->AddKeyboardCallback("Forward", GLFW_KEY_W, KeybindAction::Press, KeybindModifier::Any, nullptr);
        characterSingleton.keybindGroup->AddKeyboardCallback("Backward", GLFW_KEY_S, KeybindAction::Press, KeybindModifier::Any, nullptr);
        characterSingleton.keybindGroup->AddKeyboardCallback("Left", GLFW_KEY_A, KeybindAction::Press, KeybindModifier::Any, nullptr);
        characterSingleton.keybindGroup->AddKeyboardCallback("Right", GLFW_KEY_D, KeybindAction::Press, KeybindModifier::Any, nullptr);
        characterSingleton.keybindGroup->AddKeyboardCallback("Upwards", GLFW_KEY_SPACE, KeybindAction::Press, KeybindModifier::Any, nullptr);
        characterSingleton.keybindGroup->AddKeyboardCallback("Select Target", GLFW_MOUSE_BUTTON_LEFT, KeybindAction::Release, KeybindModifier::Any, [](i32 key, KeybindAction action, KeybindModifier modifier)
        {
            entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
            entt::registry::context& ctx = registry->ctx();
            auto& characterSingleton = ctx.get<Singletons::CharacterSingleton>();

            if (characterSingleton.moverEntity == entt::null)
                return false;

            entt::entity targetEntity = entt::null;
            if (::Util::Physics::GetEntityAtMousePosition(ServiceLocator::GetEditorHandler()->GetViewport(), targetEntity))
            {
                if (targetEntity != characterSingleton.moverEntity)
                {
                    if (registry->valid(targetEntity) && !registry->all_of<Components::NetworkedEntity>(targetEntity))
                    {
                        targetEntity = entt::null;
                    }
                }
            }

            auto& networkedEntity = registry->get<Components::NetworkedEntity>(characterSingleton.moverEntity);
            if (networkedEntity.targetEntity == targetEntity)
                return false;

            Singletons::NetworkState& networkState = ctx.get<Singletons::NetworkState>();
            if (!networkState.entityToNetworkID.contains(targetEntity))
                return false;

            if (networkState.client && networkState.client->IsConnected())
            {
                std::shared_ptr<Bytebuffer> buffer = Bytebuffer::Borrow<128>();
                entt::entity targetNetworkID = networkState.entityToNetworkID[targetEntity];

                if (Util::MessageBuilder::Entity::BuildTargetUpdateMessage(buffer, targetNetworkID))
                {
                    networkState.client->Send(buffer);
                }
            }

            networkedEntity.targetEntity = targetEntity;
            return true;
        });

        characterSingleton.cameraToggleKeybindGroup->SetActive(true);
        characterSingleton.cameraToggleKeybindGroup->AddKeyboardCallback("Toggle Camera Mode", GLFW_KEY_C, KeybindAction::Press, KeybindModifier::Any, [](i32 key, KeybindAction action, KeybindModifier modifier)
        {
            entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
            entt::registry::context& ctx = registry->ctx();

            auto& activeCamera = ctx.get<Singletons::ActiveCamera>();
            auto& characterSingleton = ctx.get<Singletons::CharacterSingleton>();
            auto& freeFlyingCameraSettings = ctx.get<Singletons::FreeflyingCameraSettings>();
            auto& orbitalCameraSettings = ctx.get<Singletons::OrbitalCameraSettings>();

            InputManager* inputManager = ServiceLocator::GetInputManager();

            KeybindGroup* freeFlyingCameraKeybindGroup = inputManager->GetKeybindGroupByHash("FreeFlyingCamera"_h);
            KeybindGroup* orbitalCameraKeybindGroup = inputManager->GetKeybindGroupByHash("OrbitalCamera"_h);

            if (activeCamera.entity == orbitalCameraSettings.entity)
            {
                if (!registry->valid(freeFlyingCameraSettings.entity))
                    return false;

                Util::CameraUtil::SetCaptureMouse(false); // Uncapture mouse for Orbital Camera when switching to FreeFlying Camera
                activeCamera.entity = freeFlyingCameraSettings.entity;

                characterSingleton.keybindGroup->SetActive(false);
                orbitalCameraKeybindGroup->SetActive(false);

                freeFlyingCameraKeybindGroup->SetActive(true);
                Util::CameraUtil::SetCaptureMouse(false); // Uncapture mouse for FreeFlying Camera when switching to FreeFlying Camera

                auto& camera = registry->get<Components::Camera>(activeCamera.entity);
                camera.dirtyView = true;
                camera.dirtyPerspective = true;
            }
            else if (activeCamera.entity == freeFlyingCameraSettings.entity)
            {
                if (!registry->valid(orbitalCameraSettings.entity))
                    return false;

                TransformSystem& transformSystem = ctx.get<TransformSystem>();

                transformSystem.ParentEntityTo(characterSingleton.controllerEntity, orbitalCameraSettings.entity);
                transformSystem.SetLocalPosition(orbitalCameraSettings.entity, orbitalCameraSettings.cameraCurrentZoomOffset);

                Util::CameraUtil::SetCaptureMouse(false); // Uncapture mouse for FreeFlyingCamera when switching to orbital camera
                activeCamera.entity = orbitalCameraSettings.entity;

                freeFlyingCameraKeybindGroup->SetActive(false);

                characterSingleton.keybindGroup->SetActive(true);
                orbitalCameraKeybindGroup->SetActive(true);
                Util::CameraUtil::SetCaptureMouse(false); // Uncapture mouse for Orbital Camera when switching to orbital camer

                auto& camera = registry->get<Components::Camera>(activeCamera.entity);
                camera.dirtyView = true;
                camera.dirtyPerspective = true;
            }

            return true;
        });
        characterSingleton.cameraToggleKeybindGroup->AddKeyboardCallback("Move Character To Camera", GLFW_KEY_G, KeybindAction::Press, KeybindModifier::Any, [](i32 key, KeybindAction action, KeybindModifier modifier)
        {
            entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
            entt::registry::context& ctx = registry->ctx();

            auto& characterSingleton = ctx.get<Singletons::CharacterSingleton>();
            auto& activeCamera = ctx.get<Singletons::ActiveCamera>();
            auto& freeFlyingCameraSettings = ctx.get<Singletons::FreeflyingCameraSettings>();

            if (activeCamera.entity != freeFlyingCameraSettings.entity)
                return false;

            TransformSystem& transformSystem = ctx.get<TransformSystem>();

            auto& camera = registry->get<Components::Camera>(freeFlyingCameraSettings.entity);
            auto& cameraTransform = registry->get<Components::Transform>(freeFlyingCameraSettings.entity);
            auto& characterMovementInfo = registry->get<Components::MovementInfo>(characterSingleton.moverEntity);
            auto& characterNetworkedEntity = registry->get<Components::NetworkedEntity>(characterSingleton.moverEntity);

            vec3 newPosition = cameraTransform.GetWorldPosition();
            if (characterSingleton.character)
            {
                characterSingleton.character->SetLinearVelocity(JPH::Vec3::sZero());
                characterSingleton.character->SetPosition(JPH::RVec3Arg(newPosition.x, newPosition.y, newPosition.z));
            }
            transformSystem.SetWorldPosition(characterSingleton.controllerEntity, newPosition);

            characterMovementInfo.pitch = 0.0f;
            characterMovementInfo.yaw = glm::pi<f32>() + glm::radians(camera.yaw);
            characterNetworkedEntity.positionOrRotationIsDirty = true;

            return true;
        });
    }

#if USE_CHARACTER_CONTROLLER_V2
    void CharacterController::Update(entt::registry& registry, f32 deltaTime)
    {
        InputManager* inputManager = ServiceLocator::GetInputManager();
        KeybindGroup* keybindGroup = inputManager->GetKeybindGroupByHash("CharacterController"_h);

        Util::EventUtil::OnEvent<Components::MapLoadedEvent>([&](const Components::MapLoadedEvent& event)
        {
            InitCharacterController(registry);
        });

        if (!keybindGroup->IsActive())
            return;

        entt::registry::context& ctx = registry.ctx();
        auto& orbitalCameraSettings = ctx.get<Singletons::OrbitalCameraSettings>();
        bool isInputForwardDown = keybindGroup->IsKeybindPressed("Forward"_h) || (orbitalCameraSettings.mouseLeftDown && orbitalCameraSettings.mouseRightDown);
        bool isInputBackwardDown = keybindGroup->IsKeybindPressed("Backward"_h);
        bool isInputLeftDown = keybindGroup->IsKeybindPressed("Left"_h);
        bool isInputRightDown = keybindGroup->IsKeybindPressed("Right"_h);

        bool isMovingForward = (isInputForwardDown && !isInputBackwardDown);
        bool isMovingBackward = (isInputBackwardDown && !isInputForwardDown);
        bool isMovingLeft = (isInputLeftDown && !isInputRightDown);
        bool isMovingRight = (isInputRightDown && !isInputLeftDown);
        bool isMoving = isMovingForward || isMovingBackward || isMovingLeft || isMovingRight;

        auto& characterSingleton = ctx.get<Singletons::CharacterSingleton>();
        auto& characterMovementInfo = registry.get<Components::MovementInfo>(characterSingleton.modelEntity);
        auto& characterModelTransform = registry.get<Components::Transform>(characterSingleton.modelEntity);

        DebugRenderer* debugRenderer = ServiceLocator::GetGameRenderer()->GetDebugRenderer();
        auto& transformSystem = TransformSystem::Get(registry);
        auto& joltState = ctx.get<Singletons::JoltState>();
        static vec3 Gravity = vec3(0.0f, -19.291105f, 0.0f);

        quat characterRotation = quat(vec3(characterMovementInfo.pitch, characterMovementInfo.yaw, 0.0f));
        transformSystem.SetWorldRotation(characterSingleton.modelEntity, characterRotation);

        vec3 characterModelStartPosition = characterModelTransform.GetWorldPosition();
        vec3 characterModelEndPosition = characterModelStartPosition;
        quat characterModelRotation = characterModelTransform.GetWorldRotation();

        vec3 horizontalMoveVector = vec3((isMovingLeft * 1.0f) + (isMovingRight * -1.0f), 0.0f, (isMovingForward * -1.0f) + (isMovingBackward * 1.0f));
        vec3 horizontalVelocity = vec3(0.0f);
        if (isMoving)
        {
            horizontalMoveVector = glm::normalize(horizontalMoveVector);
        }

        bool wasGrounded = characterMovementInfo.movementFlags.grounded;
        bool isGrounded = false;

        // Check if we are still grounded
        if (wasGrounded)
        {
            vec3 rayStart = characterModelStartPosition + Components::Transform::WORLD_UP;
            vec3 rayDirection = -Components::Transform::WORLD_UP * 2.0f;
            JPH::RayCastResult hitResult;

            if (::Util::Physics::CastRay(joltState.physicsSystem, rayStart, rayDirection, hitResult))
            {
                if (hitResult.mFraction <= 0.51f)
                {
                    vec3 hitPos = rayStart + (hitResult.mFraction * rayDirection);

                    characterModelStartPosition = hitPos;
                    isGrounded = true;

                    if (isMoving)
                    {
                        if (JPH::Body* body = joltState.physicsSystem.GetBodyLockInterface().TryGetBody(hitResult.mBodyID))
                        {
                            JPH::Vec3 surfaceNormal = body->GetWorldSpaceSurfaceNormal(hitResult.mSubShapeID2, JPH::Vec3(characterModelStartPosition.x, characterModelStartPosition.y, characterModelStartPosition.z));
                            vec3 surfaceNormalVec = glm::normalize(vec3(surfaceNormal.GetX(), surfaceNormal.GetY(), surfaceNormal.GetZ()));

                            vec3 localForward = glm::normalize(characterModelTransform.GetLocalForward());
                            vec3 rightVec = glm::normalize(glm::cross(surfaceNormalVec, localForward));
                            vec3 forwardVec = glm::normalize(glm::cross(rightVec, surfaceNormalVec));

                            // Draw Surface Normal, RightVec, ForwardVec
                            debugRenderer->DrawLine3D(hitPos, hitPos + surfaceNormalVec, Color::Green);
                            debugRenderer->DrawLine3D(characterModelStartPosition, characterModelStartPosition + rightVec, Color::Red);
                            debugRenderer->DrawLine3D(characterModelStartPosition, characterModelStartPosition + forwardVec, Color::Blue);

                            horizontalVelocity = forwardVec * horizontalMoveVector.z + rightVec * horizontalMoveVector.x;
                        };
                    }
                }
            }
        }

        if (isGrounded)
        {
            if (isMoving)
            {
                vec3 velocity = horizontalVelocity * characterMovementInfo.speed;
                vec3 verticalVelocity = vec3(0.0f, 0.0f, 0.0f);

                vec3 horizontalMove = velocity * deltaTime;

                vec3 rayStart = characterModelStartPosition + Components::Transform::WORLD_UP;
                vec3 rayDirection = horizontalMove;
                JPH::RayCastResult hitResult;

                if (::Util::Physics::CastRay(joltState.physicsSystem, rayStart, rayDirection, hitResult))
                {
                    vec3 hitPos = rayStart + (hitResult.mFraction * rayDirection);
                    characterModelEndPosition = hitPos;
                }
                else
                {
                    characterModelEndPosition = characterModelStartPosition + horizontalMove;
                }
            }
        }
        else
        {
            vec3 gravityDirection = Gravity * deltaTime;

            JPH::Vec3 start = JPH::Vec3(characterModelStartPosition.x, characterModelStartPosition.y + 1.0f, characterModelStartPosition.z);
            JPH::Vec3 direction = JPH::Vec3(gravityDirection.x, gravityDirection.y + -1.0f, gravityDirection.z);

            JPH::RRayCast ray(start, direction);
            JPH::RayCastResult hit;

            if (joltState.physicsSystem.GetNarrowPhaseQuery().CastRay(ray, hit))
            {
                JPH::Vec3 hitPos = ray.GetPointOnRay(hit.mFraction);
                vec3 hitPosition = vec3(hitPos.GetX(), hitPos.GetY(), hitPos.GetZ());

                characterModelEndPosition = hitPosition;
                isGrounded = true;
            }
            else
            {
                characterModelEndPosition = characterModelStartPosition + gravityDirection;
            }
        }

        characterMovementInfo.movementFlags.grounded = isGrounded;
        transformSystem.SetWorldPosition(characterSingleton.entity, characterModelEndPosition);
    }

    void CharacterController::InitCharacterController(entt::registry& registry)
    {
        entt::registry::context& ctx = registry.ctx();

        auto& characterSingleton = ctx.get<Singletons::CharacterSingleton>();
        auto& movementInfo = registry.get<Components::MovementInfo>(characterSingleton.modelEntity);

        ModelLoader* modelLoader = ServiceLocator::GetGameRenderer()->GetModelLoader();
        modelLoader->LoadDisplayIDForEntity(characterSingleton.modelEntity, 50);

        MapLoader* mapLoader = ServiceLocator::GetGameRenderer()->GetMapLoader();
        JPH::Vec3 newPosition = JPH::Vec3(-1500.0f, 350.0f, 1250.0f);

        if (mapLoader->GetCurrentMapID() == std::numeric_limits<u32>().max())
        {
            newPosition = JPH::Vec3(0.0f, 0.0f, 0.0f);
        }

        characterSingleton.targetEntity = entt::null;
        characterSingleton.positionOrRotationUpdateTimer = 0.0f;
        characterSingleton.positionOrRotationIsDirty = true;
        characterSingleton.canControlInAir = true;

        movementInfo.pitch = 0.0f;
        movementInfo.yaw = 0.0f;
        movementInfo.speed = 7.1111f;
        movementInfo.jumpSpeed = 7.9555f;
        movementInfo.gravityModifier = 1.0f;
        movementInfo.jumpState = Components::JumpState::None;

        auto& transformSystem = ctx.get<TransformSystem>();
        auto& orbitalCameraSettings = ctx.get<Singletons::OrbitalCameraSettings>();

        transformSystem.ParentEntityTo(characterSingleton.entity, orbitalCameraSettings.entity);
        transformSystem.SetLocalPosition(orbitalCameraSettings.entity, orbitalCameraSettings.cameraCurrentZoomOffset);
        transformSystem.SetWorldPosition(characterSingleton.entity, vec3(newPosition.GetX(), newPosition.GetY(), newPosition.GetZ()));
    }

#else
    void CharacterController::Update(entt::registry& registry, f32 deltaTime)
    {
        ZoneScopedN("CharacterController::Preprocessing");
        InputManager* inputManager = ServiceLocator::GetInputManager();
        KeybindGroup* keybindGroup = inputManager->GetKeybindGroupByHash("CharacterController"_h);

        entt::registry::context& ctx = registry.ctx();
        auto& characterSingleton = ctx.get<Singletons::CharacterSingleton>();
        auto& networkState = ctx.get<Singletons::NetworkState>();

        Util::EventUtil::OnEvent<Components::MapLoadedEvent>([&](const Components::MapLoadedEvent& event)
        {
            bool isLocal = !networkState.client || (networkState.client && !networkState.client->IsConnected());
            InitCharacterController(registry, isLocal);
        });

//#ifdef JPH_DEBUG_RENDERER
//        // TODO: Fix Jolt Primitives being erased in JoltDebugRenderer causing crash when changing map
//        auto& transform = registry.get<Components::Transform>(characterSingleton.modelEntity);
//
//        JoltDebugRenderer* joltDebugRenderer = ServiceLocator::GetGameRenderer()->GetJoltDebugRenderer();
//
//        vec3 modelScale = transform.GetLocalScale();
//        characterSingleton.character->GetShape()->Draw(joltDebugRenderer, characterSingleton.character->GetWorldTransform(), JPH::Vec3(modelScale.x, modelScale.y, modelScale.z), JPH::Color::sCyan, true, true);
//#endif

        if (!keybindGroup->IsActive() || characterSingleton.moverEntity == entt::null)
            return;

        auto& transformSystem = ctx.get<TransformSystem>();

        auto& joltState = ctx.get<Singletons::JoltState>();
        static constexpr f32 fixedDeltaTime = Singletons::JoltState::FixedDeltaTime;
        if (joltState.updateTimer < fixedDeltaTime)
            return;

        {
            ZoneScopedN("CharacterController::Update - Active");

            auto& orbitalCameraSettings = ctx.get<Singletons::OrbitalCameraSettings>();
            auto& model = registry.get<Components::Model>(characterSingleton.moverEntity);
            auto& movementInfo = registry.get<Components::MovementInfo>(characterSingleton.moverEntity);
            auto& networkedEntity = registry.get<Components::NetworkedEntity>(characterSingleton.moverEntity);
            auto& unitStatsComponent = registry.get<Components::UnitStatsComponent>(characterSingleton.moverEntity);

            static JPH::Vec3 gravity = JPH::Vec3(0.0f, -19.291105f, 0.0f);
            JPH::Vec3 moveDirection = JPH::Vec3(0.0f, 0.0f, 0.0f);

            bool isInputForwardDown = keybindGroup->IsKeybindPressed("Forward"_h) || (orbitalCameraSettings.mouseLeftDown && orbitalCameraSettings.mouseRightDown);
            bool isInputBackwardDown = keybindGroup->IsKeybindPressed("Backward"_h);
            bool isInputLeftDown = keybindGroup->IsKeybindPressed("Left"_h);
            bool isInputRightDown = keybindGroup->IsKeybindPressed("Right"_h);

            bool isAlive = unitStatsComponent.currentHealth > 0.0f;
            bool isMovingForward = (isInputForwardDown && !isInputBackwardDown) * isAlive;
            bool isMovingBackward = (isInputBackwardDown && !isInputForwardDown) * isAlive;
            bool isMovingLeft = (isInputLeftDown && !isInputRightDown) * isAlive;
            bool isMovingRight = (isInputRightDown && !isInputLeftDown) * isAlive;
            bool isMoving = isInputForwardDown || isInputBackwardDown || isInputLeftDown || isInputRightDown;

            moveDirection += isMovingForward * JPH::Vec3(0.0f, 0.0f, -1.0f);
            moveDirection += isMovingBackward * JPH::Vec3(0.0f, 0.0f, 1.0f);
            moveDirection += isMovingLeft * JPH::Vec3(1.0f, 0.0f, 0.0f);;
            moveDirection += isMovingRight * JPH::Vec3(-1.0f, 0.0f, 0.0f);

            quat characterRotation = quat(vec3(movementInfo.pitch, movementInfo.yaw, 0.0f));
            if (isAlive)
            {
                ZoneScopedN("CharacterController::Update - UpdateRotation");
                transformSystem.SetWorldRotation(characterSingleton.moverEntity, characterRotation);

                JPH::Quat joltRotation = JPH::Quat(characterRotation.x, characterRotation.y, characterRotation.z, characterRotation.w);
                characterSingleton.character->SetRotation(joltRotation);
            }

            Components::MovementFlags previousMovementFlags = movementInfo.movementFlags;

            movementInfo.movementFlags.forward = isMovingForward;
            movementInfo.movementFlags.backward = isMovingBackward;
            movementInfo.movementFlags.left = isMovingLeft;
            movementInfo.movementFlags.right = isMovingRight;
            movementInfo.movementFlags.justGrounded = false;
            movementInfo.movementFlags.justEndedJump = false;

            JPH::CharacterVirtual::EGroundState groundState = characterSingleton.character->GetGroundState();

            // Fix for animations bricking when turning off animations while jumping state is not None
            bool animationsEnabled = *CVarSystem::Get()->GetIntCVar(CVarCategory::Client | CVarCategory::Rendering, "animationEnabled");
            if (!animationsEnabled && movementInfo.jumpState != Components::JumpState::None)
            {
                movementInfo.jumpState = Components::JumpState::None;
            }

            // TODO : When jumping, we need to incoporate checks from the physics system to handle if jumping ends early
            bool isGrounded = groundState == JPH::CharacterVirtual::EGroundState::OnGround && movementInfo.verticalVelocity <= 0.0f;
            bool canJump = isAlive && isGrounded && (!movementInfo.movementFlags.jumping && movementInfo.jumpState == Components::JumpState::None);
            bool isTryingToJump = keybindGroup->IsKeybindPressed("Upwards"_h) && canJump;

            JPH::Quat virtualCharacterRotation = JPH::Quat(characterRotation.x, characterRotation.y, characterRotation.z, characterRotation.w);
            if (!moveDirection.IsNearZero())
            {
                moveDirection = virtualCharacterRotation * moveDirection;
                moveDirection = moveDirection.Normalized();
            }

            f32 speed = movementInfo.speed;
            if (isMovingBackward)
                speed *= 0.5f;

            JPH::Vec3 currentVelocity = characterSingleton.character->GetLinearVelocity();
            JPH::Vec3 desiredVelocity = characterSingleton.character->GetGroundVelocity() + (moveDirection * speed);
            JPH::Vec3 newVelocity = JPH::Vec3(0.0f, 0.0f, 0.0f);

            if (!desiredVelocity.IsNearZero() || currentVelocity.GetY() < 0.0f || !isGrounded)
            {
                desiredVelocity.SetY(currentVelocity.GetY());
            }

            bool canControlInAir = isGrounded || characterSingleton.canControlInAir;
            characterSingleton.canControlInAir = canControlInAir;

            if (isGrounded || (canControlInAir && isMoving))
            {
                ZoneScopedN("CharacterController::Update - Update Movement Velocity");
                if (isGrounded && desiredVelocity.GetY() < 0.0f)
                {
                    desiredVelocity.SetY(0.0f);
                }

                newVelocity = desiredVelocity;

                if (isTryingToJump)
                {
                    f32 jumpSpeed = movementInfo.jumpSpeed * movementInfo.gravityModifier;
                    newVelocity += JPH::Vec3(0.0f, jumpSpeed, 0.0f);

                    movementInfo.movementFlags.jumping = true;
                    movementInfo.jumpState = Components::JumpState::Begin;
                }
                else
                {
                    characterSingleton.canControlInAir = false;
                }
            }
            else
            {
                ZoneScopedN("CharacterController::Update - Calculate Fall Velocity");
                newVelocity = currentVelocity + ((gravity * movementInfo.gravityModifier) * fixedDeltaTime);
            }

            characterSingleton.character->SetLinearVelocity(newVelocity);

            ::Util::CharacterController::UpdateSettings updateSettings =
            {
                .mStickToFloorStepDown = vec3(0.0f, -0.2f, 0.0f),
                .mWalkStairsStepUp = vec3(0.0f, 1.1918f, 0.0f),
                .mWalkStairsStepDownExtra = vec3(0.0f, 0.0f, 0.0f)
            };

            JPH::DefaultBroadPhaseLayerFilter broadPhaseLayerFilter(joltState.objectVSBroadPhaseLayerFilter, Jolt::Layers::MOVING);
            JPH::DefaultObjectLayerFilter objectLayerFilter(joltState.objectVSObjectLayerFilter, Jolt::Layers::MOVING);
            JPH::BodyFilter bodyFilter;
            JPH::ShapeFilter shapeFilter;

            {
                ZoneScopedN("CharacterController::Update - Physics Update");
                ::Util::CharacterController::Update(characterSingleton.character, fixedDeltaTime, gravity, updateSettings, broadPhaseLayerFilter, objectLayerFilter, bodyFilter, shapeFilter, joltState.allocator);
            }
            JPH::Vec3 position = characterSingleton.character->GetPosition();
            transformSystem.SetWorldPosition(characterSingleton.controllerEntity, vec3(position.GetX(), position.GetY(), position.GetZ()));

            JPH::Vec3 linearVelocity = characterSingleton.character->GetLinearVelocity();
            movementInfo.horizontalVelocity = vec2(linearVelocity.GetX(), linearVelocity.GetZ());
            movementInfo.verticalVelocity = linearVelocity.GetY();

            groundState = characterSingleton.character->GetGroundState();

            bool wasGrounded = isGrounded;
            isGrounded = groundState == JPH::CharacterVirtual::EGroundState::OnGround;

            movementInfo.movementFlags.grounded = isGrounded;
            if (isGrounded)
            {
                if (!wasGrounded)
                {
                    networkedEntity.positionOrRotationIsDirty = true;
                    movementInfo.movementFlags.justGrounded = true;
                }

                if (movementInfo.movementFlags.jumping || movementInfo.jumpState != Components::JumpState::None)
                {
                    movementInfo.movementFlags.jumping = false;
                    movementInfo.movementFlags.justEndedJump = true;
                    movementInfo.jumpState = Components::JumpState::None;
                }
            }

            bool wasMoving = previousMovementFlags.forward || previousMovementFlags.backward || previousMovementFlags.left || previousMovementFlags.right;

            if (!isGrounded || isMoving || wasMoving || networkedEntity.positionOrRotationIsDirty)
            {
                ZoneScopedN("CharacterController::Update - Network Update");
                // Just started moving
                auto& networkState = ctx.get<Singletons::NetworkState>();
                if (networkState.client && networkState.client->IsConnected())
                {
                    ZoneScopedN("CharacterController::Update - Network Update - Building Message");
                    auto& transform = registry.get<Components::Transform>(characterSingleton.moverEntity);
                    std::shared_ptr<Bytebuffer> buffer = Bytebuffer::Borrow<128>();
                    if (Util::MessageBuilder::Entity::BuildMoveMessage(buffer, transform.GetWorldPosition(), transform.GetWorldRotation(), movementInfo.movementFlags, movementInfo.verticalVelocity))
                    {
                        ZoneScopedN("CharacterController::Update - Network Update - Sending Message");
                        networkState.client->Send(buffer);
                    }
                }

                networkedEntity.positionOrRotationIsDirty = false;
            }
        }
    }

    void CharacterController::InitCharacterController(entt::registry& registry, bool isLocal)
    {
        entt::registry::context& ctx = registry.ctx();

        auto& joltState = ctx.get<Singletons::JoltState>();
        auto& transformSystem = ctx.get<TransformSystem>();
        auto& activeCamera = ctx.get<Singletons::ActiveCamera>();
        auto& orbitalCameraSettings = ctx.get<Singletons::OrbitalCameraSettings>();
        auto& characterSingleton = ctx.get<Singletons::CharacterSingleton>();

        if (isLocal)
        {
            characterSingleton.moverEntity = registry.create();
            registry.emplace<Components::Name>(characterSingleton.moverEntity);
            registry.emplace<Components::AABB>(characterSingleton.moverEntity);
            registry.emplace<Components::Transform>(characterSingleton.moverEntity);
            registry.emplace<Components::Model>(characterSingleton.moverEntity);
            registry.emplace<Components::MovementInfo>(characterSingleton.moverEntity);
            registry.emplace<Components::NetworkedEntity>(characterSingleton.moverEntity);

            auto& displayInfo = registry.emplace<Components::DisplayInfo>(characterSingleton.moverEntity);
            displayInfo.displayID = 50;

            auto& unitStatsComponent = registry.emplace<Components::UnitStatsComponent>(characterSingleton.moverEntity);
            unitStatsComponent.currentHealth = 50.0f;
            unitStatsComponent.maxHealth = 100.0f;
            transformSystem.SetWorldPosition(characterSingleton.moverEntity, vec3(0.0f, 0.0f, 0.0f));

            ModelLoader* modelLoader = ServiceLocator::GetGameRenderer()->GetModelLoader();
            modelLoader->LoadDisplayIDForEntity(characterSingleton.moverEntity, 50);
        }

        f32 width = 0.4166f;
        f32 height = 1.9134f;
        f32 pyramidHeight = 0.25f;

        JPH::Array<JPH::Vec3> points =
        {
            // Top of the Box
            {-width / 2.0f, height + pyramidHeight, -width / 2.0f},
            {-width / 2.0f, height + pyramidHeight,  width / 2.0f},
            { width / 2.0f, height + pyramidHeight,  width / 2.0f},
            { width / 2.0f, height + pyramidHeight, -width / 2.0f},

            // Bottom of the Box
            {-width / 2.0f, pyramidHeight, -width / 2.0f },
            {-width / 2.0f, pyramidHeight,  width / 2.0f },
            { width / 2.0f, pyramidHeight,  width / 2.0f },
            { width / 2.0f, pyramidHeight, -width / 2.0f }
        };

        // Apex of the pyramid
        points.push_back({ 0.0f, 0.0f, 0.0f });

        JPH::ConvexHullShapeSettings shapeSetting = JPH::ConvexHullShapeSettings(points, 0.0f);
        JPH::ShapeSettings::ShapeResult shapeResult = shapeSetting.Create();

        static constexpr f32 MaxWallClimbAngle = glm::radians(50.0f);

        JPH::CharacterVirtualSettings characterSettings;
        characterSettings.mShape = shapeResult.Get();
        characterSettings.mBackFaceMode = JPH::EBackFaceMode::IgnoreBackFaces;
        characterSettings.mMaxSlopeAngle = MaxWallClimbAngle;

        if (characterSingleton.character)
            delete characterSingleton.character;

        characterSingleton.character = new JPH::CharacterVirtual(&characterSettings, JPH::RVec3(0.0f, 0.0f, 0.0f), JPH::Quat::sIdentity(), &joltState.physicsSystem);
        characterSingleton.character->SetShapeOffset(JPH::Vec3(0.0f, 0.0f, 0.0f));

        JPH::Vec3 newPosition = JPH::Vec3(-1500.0f, 310.0f, 1250.0f);

        MapLoader* mapLoader = ServiceLocator::GetGameRenderer()->GetMapLoader();
        if (mapLoader->GetCurrentMapID() == std::numeric_limits<u32>().max())
        {
            newPosition = JPH::Vec3(0.0f, 0.0f, 0.0f);
        }

        characterSingleton.character->SetMass(1000000.0f);
        characterSingleton.character->SetPenetrationRecoverySpeed(0.5f);
        characterSingleton.character->SetLinearVelocity(JPH::Vec3::sZero());
        characterSingleton.character->SetPosition(newPosition);

        auto& networkedInfo = registry.get<Components::NetworkedEntity>(characterSingleton.moverEntity);
        networkedInfo.positionProgress = -1.0f;
        networkedInfo.positionOrRotationIsDirty = true;
        characterSingleton.canControlInAir = true;

        auto& movementInfo = registry.get<Components::MovementInfo>(characterSingleton.moverEntity);
        movementInfo.pitch = 0.0f;
        movementInfo.yaw = 0.0f;
        movementInfo.speed = 7.1111f;
        movementInfo.jumpSpeed = 7.9555f;
        movementInfo.gravityModifier = 1.0f;
        movementInfo.jumpState = Components::JumpState::None;

        transformSystem.ParentEntityTo(characterSingleton.controllerEntity, characterSingleton.moverEntity);
        transformSystem.SetLocalPosition(characterSingleton.moverEntity, vec3(0.0f, 0.0f, 0.0f));

        transformSystem.ParentEntityTo(characterSingleton.controllerEntity, orbitalCameraSettings.entity);
        transformSystem.SetLocalPosition(orbitalCameraSettings.entity, orbitalCameraSettings.cameraCurrentZoomOffset);

        transformSystem.SetWorldPosition(characterSingleton.controllerEntity, vec3(newPosition.GetX(), newPosition.GetY(), newPosition.GetZ()));
    }
    void CharacterController::DeleteCharacterController(entt::registry& registry, bool isLocal)
    {
        entt::registry::context& ctx = registry.ctx();
        auto& characterSingleton = ctx.get<Singletons::CharacterSingleton>();

        if (characterSingleton.moverEntity != entt::null)
        {
            if (isLocal)
            {
                if (auto* model = registry.try_get<Components::Model>(characterSingleton.moverEntity))
                {
                    if (model->instanceID != std::numeric_limits<u32>().max())
                    {
                        ModelLoader* modelLoader = ServiceLocator::GetGameRenderer()->GetModelLoader();
                        modelLoader->UnloadModelForEntity(characterSingleton.moverEntity, model->instanceID);

                        Animation::AnimationSystem* animationSystem = ServiceLocator::GetAnimationSystem();
                        animationSystem->RemoveInstance(model->instanceID);
                    }
                }

                registry.destroy(characterSingleton.moverEntity);
            }

            characterSingleton.moverEntity = entt::null;
        }
    }
#endif
}