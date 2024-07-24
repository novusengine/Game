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
#include "Game/ECS/Util/Transforms.h"
#include "Game/Editor/EditorHandler.h"
#include "Game/Gameplay/MapLoader.h"
#include "Game/Rendering/GameRenderer.h"
#include "Game/Rendering/Debug/JoltDebugRenderer.h"
#include "Game/Rendering/Model/ModelLoader.h"
#include "Game/Util/CharacterControllerUtil.h"
#include "Game/Util/PhysicsUtil.h"
#include "Game/Util/UnitUtil.h"
#include "Game/Util/ServiceLocator.h"

#include <Input/InputManager.h>
#include <Input/KeybindGroup.h>

#include <Network/Client.h>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>

#include <entt/entt.hpp>
#include <GLFW/glfw3.h>
#include <Game/ECS/Util/CameraUtil.h>
#include <Base/CVarSystem/CVarSystem.h>

namespace ECS::Systems
{
    void CharacterController::Init(entt::registry& registry)
    {
        entt::registry::context& ctx = registry.ctx();

        auto& joltState = ctx.get<Singletons::JoltState>();
        auto& transformSystem = ctx.get<TransformSystem>();
        auto& characterSingleton = ctx.emplace<Singletons::CharacterSingleton>();

        characterSingleton.entity = registry.create();
        auto& name = registry.emplace<Components::Name>(characterSingleton.entity);
        name.fullName = "CharacterController";
        name.name = "CharacterController";
        name.nameHash = StringUtils::fnv1a_32(name.name.c_str(), name.name.size());
        registry.emplace<Components::Transform>(characterSingleton.entity);

        characterSingleton.modelEntity = registry.create();
        registry.emplace<Components::AABB>(characterSingleton.modelEntity);
        registry.emplace<Components::Transform>(characterSingleton.modelEntity);
        registry.emplace<Components::Name>(characterSingleton.modelEntity);
        registry.emplace<Components::Model>(characterSingleton.modelEntity);

        registry.emplace<Components::MovementInfo>(characterSingleton.modelEntity);
        auto& displayInfo = registry.emplace<Components::DisplayInfo>(characterSingleton.modelEntity);
        displayInfo.displayID = 50;

        auto& unitStatsComponent = registry.emplace<Components::UnitStatsComponent>(characterSingleton.modelEntity);
        unitStatsComponent.currentHealth = 50.0f;
        unitStatsComponent.maxHealth = 100.0f;

        transformSystem.SetWorldPosition(characterSingleton.entity, vec3(0.0f, 0.0f, 0.0f));
        transformSystem.SetWorldPosition(characterSingleton.modelEntity, vec3(0.0f, 0.0f, 0.0f));
        transformSystem.ParentEntityTo(characterSingleton.entity, characterSingleton.modelEntity);

        InitCharacterController(registry);

        InputManager* inputManager = ServiceLocator::GetInputManager();
        characterSingleton.keybindGroup = inputManager->CreateKeybindGroup("CharacterController", 100);
        characterSingleton.cameraToggleKeybindGroup = inputManager->CreateKeybindGroup("CharacterController - Camera Toggle", 1000);
        characterSingleton.keybindGroup->SetActive(false);

        characterSingleton.keybindGroup->AddKeyboardCallback("Forward", GLFW_KEY_W, KeybindAction::Press, KeybindModifier::Any, nullptr);
        characterSingleton.keybindGroup->AddKeyboardCallback("Backward", GLFW_KEY_S, KeybindAction::Press, KeybindModifier::Any, nullptr);
        characterSingleton.keybindGroup->AddKeyboardCallback("Left", GLFW_KEY_A, KeybindAction::Press, KeybindModifier::Any, nullptr);
        characterSingleton.keybindGroup->AddKeyboardCallback("Right", GLFW_KEY_D, KeybindAction::Press, KeybindModifier::Any, nullptr);
        characterSingleton.keybindGroup->AddKeyboardCallback("Upwards", GLFW_KEY_SPACE, KeybindAction::Press, KeybindModifier::Any, nullptr);
        characterSingleton.keybindGroup->AddKeyboardCallback("Select Target", GLFW_MOUSE_BUTTON_LEFT, KeybindAction::Release, KeybindModifier::Any, [&](i32 key, KeybindAction action, KeybindModifier modifier)
        {
            entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;

            entt::entity targetEntity = entt::null;
            if (::Util::Physics::GetEntityAtMousePosition(ServiceLocator::GetEditorHandler()->GetViewport(), targetEntity))
            {
                if (targetEntity != characterSingleton.entity)
                {
                    if (registry->valid(targetEntity) && !registry->all_of<Components::NetworkedEntity>(targetEntity))
                    {
                        targetEntity = entt::null;
                    }
                }
            }

            if (characterSingleton.targetEntity == targetEntity)
                return false;

            Singletons::NetworkState& networkState = registry->ctx().get<Singletons::NetworkState>();
            if (networkState.client && networkState.client->IsConnected())
            {
                std::shared_ptr<Bytebuffer> buffer = Bytebuffer::Borrow<128>();
                Network::PacketHeader header =
                {
                    .opcode = Network::Opcode::MSG_ENTITY_TARGET_UPDATE,
                    .size = sizeof(entt::entity)
                };

                entt::entity targetNetworkID = networkState.entityToNetworkID[targetEntity];
                buffer->Put(header);
                buffer->Put(targetNetworkID);

                networkState.client->Send(buffer);
            }

            characterSingleton.targetEntity = targetEntity;

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

                auto& transform = registry->get<Components::Transform>(characterSingleton.entity);
                transformSystem.ParentEntityTo(characterSingleton.entity, orbitalCameraSettings.entity);
                transformSystem.SetLocalPosition(orbitalCameraSettings.entity, orbitalCameraSettings.cameraCurrentZoomOffset);

                Util::CameraUtil::SetCaptureMouse(false); // Uncapture mouse for FreeFlyingCamera when switching to orbital camera
                activeCamera.entity = orbitalCameraSettings.entity;
                orbitalCameraSettings.entityToTrack = characterSingleton.entity;

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
            auto& characterTransform = registry->get<Components::Transform>(characterSingleton.entity);
            auto& characterMovementInfo = registry->get<Components::MovementInfo>(characterSingleton.modelEntity);

            vec3 newPosition = cameraTransform.GetWorldPosition();
            characterSingleton.character->SetLinearVelocity(JPH::Vec3::sZero());
            characterSingleton.character->SetPosition(JPH::RVec3Arg(newPosition.x, newPosition.y, newPosition.z));
            transformSystem.SetWorldPosition(characterSingleton.entity, newPosition);

            characterMovementInfo.pitch = 0.0f;
            characterMovementInfo.yaw = glm::pi<f32>() + glm::radians(camera.yaw);
            characterSingleton.positionOrRotationIsDirty = true;

            return true;
        });
    }

    void CharacterController::Update(entt::registry& registry, f32 deltaTime)
    {
        InputManager* inputManager = ServiceLocator::GetInputManager();
        KeybindGroup* keybindGroup = inputManager->GetKeybindGroupByHash("CharacterController"_h);

        entt::registry::context& ctx = registry.ctx();
        auto& characterSingleton = ctx.get<Singletons::CharacterSingleton>();
        auto& model = registry.get<Components::Model>(characterSingleton.modelEntity);
        auto& movementInfo = registry.get<Components::MovementInfo>(characterSingleton.modelEntity);

        Util::EventUtil::OnEvent<Components::MapLoadedEvent>([&](const Components::MapLoadedEvent& event)
        {
            InitCharacterController(registry);
        });


#ifdef JPH_DEBUG_RENDERER
        // TODO: Fix Jolt Primitives being erased in JoltDebugRenderer causing crash when changing map
        //auto& transform = registry.get<Components::Transform>(characterSingleton.modelEntity);
        //
        //JoltDebugRenderer* joltDebugRenderer = ServiceLocator::GetGameRenderer()->GetJoltDebugRenderer();
        //
        //mat4x4 modelMatrix = transform.GetMatrix();
        //vec3 modelScale = transform.GetLocalScale();
        //
        //JPH::Vec3 shapeOffset = characterSingleton.character->GetShapeOffset();
        //JPH::Vec4 c1 = JPH::Vec4(modelMatrix[0][0], modelMatrix[0][1], modelMatrix[0][2], modelMatrix[0][3]);
        //JPH::Vec4 c2 = JPH::Vec4(modelMatrix[1][0], modelMatrix[1][1], modelMatrix[1][2], modelMatrix[1][3]);
        //JPH::Vec4 c3 = JPH::Vec4(modelMatrix[2][0], modelMatrix[2][1], modelMatrix[2][2], modelMatrix[2][3]);
        //JPH::Vec4 c4 = JPH::Vec4(modelMatrix[3][0] + shapeOffset[0], modelMatrix[3][1] + shapeOffset[1], modelMatrix[3][2] + shapeOffset[2], modelMatrix[3][3]);
        //JPH::Mat44 joltModelMatrix = JPH::Mat44(c1, c2, c3, c4);
        //JPH::Vec3 joltModelScale = JPH::Vec3(modelScale.x, modelScale.y, modelScale.z);
        //characterSingleton.character->GetShape()->Draw(joltDebugRenderer, joltModelMatrix, joltModelScale, JPH::Color::sCyan, true, true);
#endif
        if (!keybindGroup->IsActive())
            return;

        auto& transformSystem = ctx.get<TransformSystem>();

        auto& joltState = ctx.get<Singletons::JoltState>();
        auto& orbitalCameraSettings = ctx.get<Singletons::OrbitalCameraSettings>();
        auto& unitStatsComponent = registry.get<Components::UnitStatsComponent>(characterSingleton.modelEntity);

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
        moveDirection += isMovingRight * JPH::Vec3(-1.0f, 0.0f, 0.0f);;;

        quat characterRotation = quat(vec3(movementInfo.pitch, movementInfo.yaw, 0.0f));
        if (isAlive)
        {
            transformSystem.SetWorldRotation(characterSingleton.modelEntity, characterRotation);

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
        bool isGrounded = groundState == JPH::CharacterVirtual::EGroundState::OnGround;
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
            if (desiredVelocity.GetY() < 0.0f)
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
            newVelocity = currentVelocity + ((gravity * movementInfo.gravityModifier) * deltaTime);
        }

        characterSingleton.character->SetLinearVelocity(newVelocity);

        ::Util::CharacterController::UpdateSettings updateSettings =
        {
            .mStickToFloorStepDown      = vec3(0.0f, -0.2f, 0.0f),
            .mWalkStairsStepUp          = vec3(0.0f, 1.1918f, 0.0f),
            .mWalkStairsStepDownExtra   = vec3(0.0f, 0.0f, 0.0f)
        };

        JPH::DefaultBroadPhaseLayerFilter broadPhaseLayerFilter(joltState.objectVSBroadPhaseLayerFilter, Jolt::Layers::MOVING);
        JPH::DefaultObjectLayerFilter objectLayerFilter(joltState.objectVSObjectLayerFilter, Jolt::Layers::MOVING);
        JPH::BodyFilter bodyFilter;
        JPH::ShapeFilter shapeFilter;

        ::Util::CharacterController::Update(characterSingleton.character, deltaTime, gravity, updateSettings, broadPhaseLayerFilter, objectLayerFilter, bodyFilter, shapeFilter, joltState.allocator);
        
        JPH::Vec3 position = characterSingleton.character->GetPosition();
        transformSystem.SetWorldPosition(characterSingleton.entity, vec3(position.GetX(), position.GetY(), position.GetZ()));

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
                characterSingleton.positionOrRotationIsDirty = true;
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

        static constexpr f32 positionOrRotationUpdateInterval = 1.0f / 60.0f;
        if (characterSingleton.positionOrRotationUpdateTimer >= positionOrRotationUpdateInterval)
        {
            if (!isGrounded || isMoving || wasMoving || characterSingleton.positionOrRotationIsDirty)
            {
                // Just started moving
                auto& networkState = ctx.get<Singletons::NetworkState>();
                if (networkState.client && networkState.client->IsConnected())
                {
                    auto& transform = registry.get<Components::Transform>(characterSingleton.modelEntity);

                    std::shared_ptr<Bytebuffer> buffer = Bytebuffer::Borrow<128>();
                    Network::PacketHeader header =
                    {
                        .opcode = Network::Opcode::MSG_ENTITY_MOVE,
                        .size = sizeof(vec3) + sizeof(quat) + sizeof(Components::MovementFlags) + sizeof(f32)
                    };

                    buffer->Put(header);
                    buffer->Put(transform.GetWorldPosition());
                    buffer->Put(transform.GetWorldRotation());
                    buffer->Put(movementInfo.movementFlags);
                    buffer->Put(movementInfo.verticalVelocity);

                    networkState.client->Send(buffer);
                }

                characterSingleton.positionOrRotationIsDirty = false;
            }

            characterSingleton.positionOrRotationUpdateTimer -= positionOrRotationUpdateInterval;
        }
        else
        {
            if ((isGrounded && !isMoving) && wasMoving)
            {
                characterSingleton.positionOrRotationIsDirty = true;
            }

            characterSingleton.positionOrRotationUpdateTimer += deltaTime;
        }

        auto SetOrientation = [&](vec4& settings, f32 orientation)
        {
            f32 currentOrientation = settings.x;
            if (orientation == currentOrientation)
                return;

            settings.y = orientation;
            settings.w = 0.0f;
            settings.z = 1.0f / 8.0f;
        };

        {
            ::Util::Unit::UpdateAnimationState(registry, characterSingleton.modelEntity, model.instanceID, deltaTime);
            if (isGrounded || (canControlInAir && isMoving))
            {
                f32 spineOrientation = 0.0f;
                f32 headOrientation = 0.0f;
                f32 waistOrientation = 0.0f;

                if (isMovingForward)
                {
                    if (isMovingRight)
                    {
                        spineOrientation = 30.0f;
                        headOrientation = -15.0f;
                        waistOrientation = 45.0f;
                    }
                    else if (isMovingLeft)
                    {
                        spineOrientation = -30.0f;
                        headOrientation = 15.0f;
                        waistOrientation = -45.0f;
                    }
                }
                else if (isMovingBackward)
                {
                    if (isMovingRight)
                    {
                        spineOrientation = -30.0f;
                        headOrientation = 15.0f;
                        waistOrientation = -45.0f;
                    }
                    else if (isMovingLeft)
                    {
                        spineOrientation = 30.0f;
                        headOrientation = -15.0f;
                        waistOrientation = 45.0f;
                    }
                }
                else if (isMovingRight)
                {
                    spineOrientation = 45.0f;
                    headOrientation = -30.0f;
                    waistOrientation = 90.0f;
                }
                else if (isMovingLeft)
                {
                    spineOrientation = -45.0f;
                    headOrientation = 30.0f;
                    waistOrientation = -90.0f;
                }

                SetOrientation(characterSingleton.spineRotationSettings, spineOrientation);
                SetOrientation(characterSingleton.headRotationSettings, headOrientation);
                SetOrientation(characterSingleton.waistRotationSettings, waistOrientation);
            }
        }

        auto HandleUpdateOrientation = [](vec4& settings, f32 deltaTime) -> bool
        {
            if (settings.x == settings.y)
                return false;

            settings.w += deltaTime;
            settings.w = glm::clamp(settings.w, 0.0f, settings.z);

            f32 progress = settings.w / settings.z;
            settings.x = glm::mix(settings.x, settings.y, progress);

            return true;
        };

        {
            Animation::AnimationSystem* animationSystem = ServiceLocator::GetAnimationSystem();

            if (model.modelID != std::numeric_limits<u32>().max() && model.instanceID != std::numeric_limits<u32>().max())
            {
                if (HandleUpdateOrientation(characterSingleton.spineRotationSettings, deltaTime))
                {
                    quat rotation = glm::quat(glm::vec3(0.0f, glm::radians(characterSingleton.spineRotationSettings.x), 0.0f));
                    animationSystem->SetBoneRotation(model.instanceID, Animation::Bone::SpineLow, rotation);
                }
                if (HandleUpdateOrientation(characterSingleton.headRotationSettings, deltaTime))
                {
                    quat rotation = glm::quat(glm::vec3(0.0f, glm::radians(characterSingleton.headRotationSettings.x), 0.0f));
                    animationSystem->SetBoneRotation(model.instanceID, Animation::Bone::Head, rotation);
                }
                if (HandleUpdateOrientation(characterSingleton.waistRotationSettings, deltaTime))
                {
                    quat rotation = glm::quat(glm::vec3(0.0f, glm::radians(characterSingleton.waistRotationSettings.x), 0.0f));
                    animationSystem->SetBoneRotation(model.instanceID, Animation::Bone::Waist, rotation);
                }
            }
        }
    }

    void CharacterController::InitCharacterController(entt::registry& registry)
    {
        entt::registry::context& ctx = registry.ctx();

        auto& joltState = ctx.get<Singletons::JoltState>();
        auto& transformSystem = ctx.get<TransformSystem>();
        auto& activeCamera = ctx.get<Singletons::ActiveCamera>();
        auto& orbitalCameraSettings = ctx.get<Singletons::OrbitalCameraSettings>();
        auto& characterSingleton = ctx.get<Singletons::CharacterSingleton>();
        auto& movementInfo = registry.get<Components::MovementInfo>(characterSingleton.modelEntity);

        ModelLoader* modelLoader = ServiceLocator::GetGameRenderer()->GetModelLoader();
        u32 modelHash = modelLoader->GetModelHashFromModelPath("character/human/female/humanfemale.complexmodel");
        modelLoader->LoadDisplayIDForEntity(characterSingleton.modelEntity, 50);

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

        characterSingleton.targetEntity = entt::null;
        characterSingleton.character->SetMass(1000000.0f);
        characterSingleton.character->SetPenetrationRecoverySpeed(0.5f);
        characterSingleton.character->SetLinearVelocity(JPH::Vec3::sZero());
        characterSingleton.character->SetPosition(newPosition);

        characterSingleton.positionOrRotationUpdateTimer = 0.0f;
        characterSingleton.positionOrRotationIsDirty = true;
        characterSingleton.canControlInAir = true;

        movementInfo.pitch = 0.0f;
        movementInfo.yaw = 0.0f;
        movementInfo.speed = 7.1111f;
        movementInfo.jumpSpeed = 7.9555f;
        movementInfo.gravityModifier = 1.0f;
        movementInfo.jumpState = Components::JumpState::None;

        transformSystem.ParentEntityTo(characterSingleton.entity, orbitalCameraSettings.entity);
        transformSystem.SetLocalPosition(orbitalCameraSettings.entity, orbitalCameraSettings.cameraCurrentZoomOffset);
        transformSystem.SetWorldPosition(characterSingleton.entity, vec3(newPosition.GetX(), newPosition.GetY(), newPosition.GetZ()));
    }
}