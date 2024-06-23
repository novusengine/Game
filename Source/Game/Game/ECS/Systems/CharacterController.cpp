#include "CharacterController.h"

#include "Game/Animation/AnimationSystem.h"
#include "Game/ECS/Components/AABB.h"
#include "Game/ECS/Components/Camera.h"
#include "Game/ECS/Components/Events.h"
#include "Game/ECS/Components/Model.h"
#include "Game/ECS/Components/Name.h"
#include "Game/ECS/Singletons/ActiveCamera.h"
#include "Game/ECS/Singletons/CharacterSingleton.h"
#include "Game/ECS/Singletons/FreeflyingCameraSettings.h"
#include "Game/ECS/Singletons/JoltState.h"
#include "Game/ECS/Singletons/OrbitalCameraSettings.h"
#include "Game/ECS/Util/EventUtil.h"
#include "Game/ECS/Util/Transforms.h"
#include "Game/Gameplay/MapLoader.h"
#include "Game/Rendering/GameRenderer.h"
#include "Game/Rendering/Debug/JoltDebugRenderer.h"
#include "Game/Rendering/Model/ModelLoader.h"
#include "Game/Util/ServiceLocator.h"

#include <Input/InputManager.h>
#include <Input/KeybindGroup.h>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>

#include <entt/entt.hpp>
#include <GLFW/glfw3.h>
#include <Game/ECS/Util/CameraUtil.h>

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

        characterSingleton.cameraToggleKeybindGroup->AddKeyboardCallback("Toggle Camera Mode", GLFW_KEY_C, KeybindAction::Press, KeybindModifier::Any, [](i32 key, KeybindAction action, KeybindModifier modifier)
        {
            entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
            entt::registry::context& ctx = registry->ctx();

            Singletons::ActiveCamera& activeCamera = ctx.get<Singletons::ActiveCamera>();
            Singletons::CharacterSingleton& characterSingleton = ctx.get<Singletons::CharacterSingleton>();
            Singletons::FreeflyingCameraSettings& freeFlyingCameraSettings = ctx.get<Singletons::FreeflyingCameraSettings>();
            Singletons::OrbitalCameraSettings& orbitalCameraSettings = ctx.get<Singletons::OrbitalCameraSettings>();

            InputManager* inputManager = ServiceLocator::GetInputManager();

            KeybindGroup* freeFlyingCameraKeybindGroup = inputManager->GetKeybindGroupByHash("FreeFlyingCamera"_h);
            KeybindGroup* orbitalCameraKeybindGroup = inputManager->GetKeybindGroupByHash("OrbitalCamera"_h);

            if (activeCamera.entity == orbitalCameraSettings.entity)
            {
                ECS::Util::CameraUtil::SetCaptureMouse(false); // Uncapture mouse for Orbital Camera when switching to FreeFlying Camera
                activeCamera.entity = freeFlyingCameraSettings.entity;

                characterSingleton.keybindGroup->SetActive(false);
                orbitalCameraKeybindGroup->SetActive(false);

                freeFlyingCameraKeybindGroup->SetActive(true);
                ECS::Util::CameraUtil::SetCaptureMouse(false); // Uncapture mouse for FreeFlying Camera when switching to FreeFlying Camera

                auto& camera = registry->get<ECS::Components::Camera>(activeCamera.entity);
                camera.dirtyView = true;
                camera.dirtyPerspective = true;
            }
            else if (activeCamera.entity == freeFlyingCameraSettings.entity)
            {
                TransformSystem& transformSystem = ctx.get<TransformSystem>();

                ECS::Components::Transform& transform = registry->get<ECS::Components::Transform>(characterSingleton.entity);
                transformSystem.ParentEntityTo(characterSingleton.entity, orbitalCameraSettings.entity);
                transformSystem.SetLocalPosition(orbitalCameraSettings.entity, orbitalCameraSettings.cameraCurrentZoomOffset);

                ECS::Util::CameraUtil::SetCaptureMouse(false); // Uncapture mouse for FreeFlyingCamera when switching to orbital camera
                activeCamera.entity = orbitalCameraSettings.entity;
                orbitalCameraSettings.entityToTrack = characterSingleton.entity;

                freeFlyingCameraKeybindGroup->SetActive(false);

                characterSingleton.keybindGroup->SetActive(true);
                orbitalCameraKeybindGroup->SetActive(true);
                ECS::Util::CameraUtil::SetCaptureMouse(false); // Uncapture mouse for Orbital Camera when switching to orbital camer

                auto& camera = registry->get<ECS::Components::Camera>(activeCamera.entity);
                camera.dirtyView = true;
                camera.dirtyPerspective = true;
            }

            return true;
        });

        characterSingleton.cameraToggleKeybindGroup->SetActive(true);
    }

    void OnJumpStartFinished(u32 instanceID, Animation::Type animation, Animation::Type interruptedBy)
    {
        if (animation != Animation::Type::JumpStart)
            return;

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& characterSingleton = registry->ctx().get<Singletons::CharacterSingleton>();

        if (characterSingleton.jumpState != ECS::Singletons::JumpState::Begin)
            return;

        bool wasCancelled = interruptedBy != Animation::Type::Invalid && interruptedBy != Animation::Type::Jump && interruptedBy != Animation::Type::JumpEnd && interruptedBy != Animation::Type::JumpLandRun;
        if (wasCancelled)
        {
            characterSingleton.jumpState = ECS::Singletons::JumpState::None;
            return;
        }

        characterSingleton.jumpState = ECS::Singletons::JumpState::Fall;

        Animation::AnimationSystem* animationSystem = ServiceLocator::GetAnimationSystem();
        animationSystem->SetBoneSequence(instanceID, Animation::Bone::Default, Animation::Type::Jump, Animation::Flag::None, Animation::BlendOverride::None);
    }
    void OnJumpEndFinished(u32 instanceID, Animation::Type animation, Animation::Type interruptedBy)
    {
        if (animation != Animation::Type::JumpEnd && animation != Animation::Type::JumpLandRun)
            return;

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& characterSingleton = registry->ctx().get<Singletons::CharacterSingleton>();

        if (characterSingleton.jumpState != ECS::Singletons::JumpState::End)
            return;

        characterSingleton.jumpState = ECS::Singletons::JumpState::None;
    }

    void CharacterController::Update(entt::registry& registry, f32 deltaTime)
    {
        InputManager* inputManager = ServiceLocator::GetInputManager();
        KeybindGroup* keybindGroup = inputManager->GetKeybindGroupByHash("CharacterController"_h);

        entt::registry::context& ctx = registry.ctx();
        auto& characterSingleton = ctx.get<Singletons::CharacterSingleton>();

        Util::EventUtil::OnEvent<Components::MapLoadedEvent>([&](const Components::MapLoadedEvent& event)
        {
            InitCharacterController(registry);
        });

        auto& transformSystem = ctx.get<TransformSystem>();
        quat characterRotation = quat(vec3(characterSingleton.pitch, characterSingleton.yaw, 0.0f));
        transformSystem.SetWorldRotation(characterSingleton.modelEntity, characterRotation);

#ifdef JPH_DEBUG_RENDERER
        // TODO: Fix Jolt Primitives being erased in JoltDebugRenderer causing crash when changing map
        //Components::Transform& transform = registry.get<Components::Transform>(characterSingleton.modelEntity);
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

        auto& joltState = ctx.get<Singletons::JoltState>();
        auto& orbitalCameraSettings = ctx.get<Singletons::OrbitalCameraSettings>();

        static JPH::Vec3 gravity = JPH::Vec3(0.0f, -9.81f, 0.0f);
        JPH::Vec3 velocity = JPH::Vec3(0.0f, 0.0f, 0.0f);
        JPH::Vec3 moveDirection = JPH::Vec3(0.0f, 0.0f, 0.0f);

        bool movingForward = keybindGroup->IsKeybindPressed("Forward"_h) || (orbitalCameraSettings.mouseLeftDown && orbitalCameraSettings.mouseRightDown);
        bool movingBackward = keybindGroup->IsKeybindPressed("Backward"_h);
        bool movingLeft = keybindGroup->IsKeybindPressed("Left"_h);
        bool movingRight = keybindGroup->IsKeybindPressed("Right"_h);

        if (movingForward)
            moveDirection += JPH::Vec3(0.0f, 0.0f, -1.0f);

        if (movingBackward)
            moveDirection += JPH::Vec3(0.0f, 0.0f, 1.0f);

        if (movingLeft)
            moveDirection += JPH::Vec3(1.0f, 0.0f, 0.0f);

        if (movingRight)
            moveDirection += JPH::Vec3(-1.0f, 0.0f, 0.0f);

        JPH::Quat virtualCharacterRotation = JPH::Quat(characterRotation.x, characterRotation.y, characterRotation.z, characterRotation.w);
        moveDirection = virtualCharacterRotation * moveDirection;

        bool moveForward = movingForward && !movingBackward;
        bool moveBackward = movingBackward && !movingForward;
        bool moveLeft = movingLeft && !movingRight;
        bool moveRight = movingRight && !movingLeft;

        characterSingleton.movementFlags.forward = moveForward;
        characterSingleton.movementFlags.backward = moveBackward;
        characterSingleton.movementFlags.left = moveLeft;
        characterSingleton.movementFlags.right = moveRight;

        JPH::CharacterVirtual::EGroundState groundState = characterSingleton.character->GetGroundState();
        bool isGrounded = groundState == JPH::CharacterVirtual::EGroundState::OnGround;

        // TODO : When jumping, we need to incoporate checks from the physics system to handle if jumping ends early
        bool isJumping = false;
        bool canJump = characterSingleton.jumpState == ECS::Singletons::JumpState::None || characterSingleton.jumpState == ECS::Singletons::JumpState::End;

        if (isGrounded)
        {
             if (keybindGroup->IsKeybindPressed("Upwards"_h) && canJump)
                isJumping = true;

            if (moveDirection.LengthSq() > 0.0f)
            {
                moveDirection = moveDirection.Normalized();
                moveDirection.SetY(isJumping);

                f32 moveSpeed = characterSingleton.speed;
                if (movingBackward)
                    moveSpeed *= 0.5f;

                moveDirection *= JPH::Vec3(moveSpeed, 6.0f, moveSpeed);
            }
            else if (isJumping)
            {
                moveDirection = JPH::Vec3(0.0f, 6.0f, 0.0f);
            }

            velocity = characterSingleton.character->GetGroundVelocity() + moveDirection;

            if (groundState == JPH::CharacterVirtual::EGroundState::OnSteepGround)
            {
                velocity += deltaTime * gravity;
            }
        }
        else
        {
            velocity = characterSingleton.character->GetLinearVelocity() + deltaTime * gravity;
        }

        characterSingleton.character->SetLinearVelocity(velocity);

        JPH::CharacterVirtual::ExtendedUpdateSettings updateSettings;
        JPH::DefaultBroadPhaseLayerFilter broadPhaseLayerFilter(joltState.objectVSBroadPhaseLayerFilter, Jolt::Layers::MOVING);
        JPH::DefaultObjectLayerFilter objectLayerFilter(joltState.objectVSObjectLayerFilter, Jolt::Layers::MOVING);
        JPH::BodyFilter bodyFilter;
        JPH::ShapeFilter shapeFilter;

        characterSingleton.character->ExtendedUpdate(deltaTime, gravity, updateSettings, broadPhaseLayerFilter, objectLayerFilter, bodyFilter, shapeFilter, joltState.allocator);
        JPH::Vec3 position = characterSingleton.character->GetPosition();
        transformSystem.SetWorldPosition(characterSingleton.entity, vec3(position.GetX(), position.GetY(), position.GetZ()));

        Animation::AnimationSystem* animationSystem = ServiceLocator::GetAnimationSystem();
        u32 instanceID = registry.get<Components::Model>(characterSingleton.modelEntity).instanceID;

        auto SetOrientation = [&](vec4& settings, f32 orientation)
        {
            f32 currentOrientation = settings.x;
            if (orientation == currentOrientation)
                return;

            settings.y = orientation;
            settings.w = 0.0f;

            f32 timeToChange = glm::abs(orientation - currentOrientation) / 450.0f;
            timeToChange = glm::max(timeToChange, 0.01f);
            settings.z = timeToChange;
        };
        auto TryPlayAnimation = [&animationSystem, &characterSingleton, instanceID](Animation::Bone bone, Animation::Type animationID, Animation::Flag flags = Animation::Flag::Loop, Animation::BlendOverride blendOverride = Animation::BlendOverride::Auto, Animation::AnimationCallback callback = nullptr) -> bool
        {
            bool canPlay = !animationSystem->IsPlaying(instanceID, bone, animationID);
            if (canPlay)
            {
                animationSystem->SetBoneSequence(instanceID, bone, animationID, flags, blendOverride, callback);
            }

            return canPlay;
        };

        if (isJumping)
        {
            if (TryPlayAnimation(Animation::Bone::Default, Animation::Type::JumpStart, Animation::Flag::None, Animation::BlendOverride::None, OnJumpStartFinished))
            {
                characterSingleton.jumpState = ECS::Singletons::JumpState::Begin;
            }
        }

        Animation::Type currentAnimation = Animation::Type::Invalid;
        Animation::Type nextAnimation = Animation::Type::Invalid;
        animationSystem->GetCurrentAnimation(instanceID, Animation::Bone::Default, &currentAnimation, &nextAnimation);

        bool isPlayingJump = characterSingleton.jumpState == ECS::Singletons::JumpState::Begin || characterSingleton.jumpState == ECS::Singletons::JumpState::Fall;
        if (!isPlayingJump)
        {
            bool canOverrideJumpEnd = true;
            if (characterSingleton.jumpState == ECS::Singletons::JumpState::End)
            {
                if ((currentAnimation == Animation::Type::JumpLandRun || nextAnimation == Animation::Type::JumpLandRun) && !moveBackward)
                {
                    canOverrideJumpEnd = false;
                }
            }

            if (isGrounded && canOverrideJumpEnd)
            {
                Animation::BlendOverride blendOverride = Animation::BlendOverride::Auto;
                if (currentAnimation == Animation::Type::Fall || currentAnimation == Animation::Type::Jump || currentAnimation == Animation::Type::JumpEnd || currentAnimation == Animation::Type::JumpLandRun)
                {
                    blendOverride = Animation::BlendOverride::None;
                }

                if (moveForward)
                {
                    // Run Forward
                    TryPlayAnimation(Animation::Bone::Default, Animation::Type::Run, Animation::Flag::Loop, blendOverride);
                }
                else if (moveBackward)
                {
                    // Walk Backward

                    TryPlayAnimation(Animation::Bone::Default, Animation::Type::Walkbackwards, Animation::Flag::Loop, blendOverride);
                }

                if (moveLeft || moveRight)
                {
                    if (moveForward)
                    {
                        TryPlayAnimation(Animation::Bone::Default, Animation::Type::Run, Animation::Flag::Loop, blendOverride);
                    }
                    else if (moveBackward)
                    {
                        TryPlayAnimation(Animation::Bone::Default, Animation::Type::Walkbackwards, Animation::Flag::Loop, blendOverride);
                    }
                    else
                    {
                        TryPlayAnimation(Animation::Bone::Default, Animation::Type::Run, Animation::Flag::Loop, blendOverride);
                    }
                }
            }

            {
                f32 spineOrientation = 0.0f;
                f32 headOrientation = 0.0f;
                f32 waistOrientation = 0.0f;

                if (moveForward)
                {
                    if (moveRight)
                    {
                        spineOrientation = 30.0f;
                        headOrientation = -15.0f;
                        waistOrientation = 45.0f;
                    }
                    else if (moveLeft)
                    {
                        spineOrientation = -30.0f;
                        headOrientation = 15.0f;
                        waistOrientation = -45.0f;
                    }
                }
                else if (moveBackward)
                {
                    if (moveRight)
                    {
                        spineOrientation = -30.0f;
                        headOrientation = 15.0f;
                        waistOrientation = -45.0f;
                    }
                    else if (moveLeft)
                    {
                        spineOrientation = 30.0f;
                        headOrientation = -15.0f;
                        waistOrientation = 45.0f;
                    }
                }
                else if (moveRight)
                {
                    spineOrientation = 45.0f;
                    headOrientation = -30.0f;
                    waistOrientation = 90.0f;
                }
                else if (moveLeft)
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

        bool IsFalling = groundState == JPH::CharacterVirtual::EGroundState::OnSteepGround || groundState == JPH::CharacterVirtual::EGroundState::InAir;
        if (isGrounded)
        {
            bool playJumpEnd = characterSingleton.jumpState == ECS::Singletons::JumpState::Fall || (characterSingleton.jumpState == ECS::Singletons::JumpState::End && currentAnimation != Animation::Type::JumpLandRun);
            if (playJumpEnd)
            {
                bool canPlayJumpEnd = characterSingleton.jumpState == ECS::Singletons::JumpState::Fall;
                if (canPlayJumpEnd)
                {
                    if (!moveBackward && (moveForward || moveRight || moveLeft))
                    {
                        if (TryPlayAnimation(Animation::Bone::Default, Animation::Type::JumpLandRun, Animation::Flag::Freeze, Animation::BlendOverride::None, OnJumpEndFinished))
                        {
                            characterSingleton.jumpState = ECS::Singletons::JumpState::End;
                        }
                    }
                    else
                    {
                        if (moveBackward)
                        {
                            if (TryPlayAnimation(Animation::Bone::Default, Animation::Type::Walkbackwards, Animation::Flag::Loop, Animation::BlendOverride::Start))
                            {
                                characterSingleton.jumpState = ECS::Singletons::JumpState::None;
                            }
                        }
                        else
                        {
                            if (TryPlayAnimation(Animation::Bone::Default, Animation::Type::JumpEnd, Animation::Flag::None, Animation::BlendOverride::None, OnJumpEndFinished))
                            {
                                characterSingleton.jumpState = ECS::Singletons::JumpState::End;
                            }
                        }
                    }
                }
            }
            else
            {
                if (!isPlayingJump && !moveForward && !moveBackward && !moveRight && !moveLeft)
                {
                    TryPlayAnimation(Animation::Bone::Default, Animation::Type::Stand);
                }
            }
        }
        else if (IsFalling)
        {
            if (characterSingleton.jumpState == ECS::Singletons::JumpState::None || characterSingleton.jumpState == ECS::Singletons::JumpState::Fall)
            {
                Animation::BlendOverride blendOverride = Animation::BlendOverride::None;

                if (characterSingleton.jumpState == ECS::Singletons::JumpState::Fall)
                    blendOverride = Animation::BlendOverride::Start;

                TryPlayAnimation(Animation::Bone::Default, Animation::Type::Fall, Animation::Flag::Loop, blendOverride);
            }
        }
        if (!isPlayingJump)
        {
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

        if (HandleUpdateOrientation(characterSingleton.spineRotationSettings, deltaTime))
        {
            quat rotation = glm::quat(glm::vec3(0.0f, glm::radians(characterSingleton.spineRotationSettings.x), 0.0f));
            animationSystem->SetBoneRotation(instanceID, Animation::Bone::SpineLow, rotation);
        }
        if (HandleUpdateOrientation(characterSingleton.headRotationSettings, deltaTime))
        {
            quat rotation = glm::quat(glm::vec3(0.0f, glm::radians(characterSingleton.headRotationSettings.x), 0.0f));
            animationSystem->SetBoneRotation(instanceID, Animation::Bone::Head, rotation);
        }
        if (HandleUpdateOrientation(characterSingleton.waistRotationSettings, deltaTime))
        {
            quat rotation = glm::quat(glm::vec3(0.0f, glm::radians(characterSingleton.waistRotationSettings.x), 0.0f));
            animationSystem->SetBoneRotation(instanceID, Animation::Bone::Waist, rotation);
        }
    }

    void CharacterController::InitCharacterController(entt::registry& registry)
    {
        entt::registry::context& ctx = registry.ctx();

        auto& joltState = ctx.get<Singletons::JoltState>();
        auto& transformSystem = ctx.get<TransformSystem>();
        auto& activeCamera = ctx.get<Singletons::ActiveCamera>();
        auto& orbitalCameraSettings = ctx.get<Singletons::OrbitalCameraSettings>();
        auto& characterSingleton = ctx.emplace<Singletons::CharacterSingleton>();

        ModelLoader* modelLoader = ServiceLocator::GetGameRenderer()->GetModelLoader();
        u32 modelHash = modelLoader->GetModelHashFromModelPath("character/human/female/humanfemale.complexmodel");
        modelLoader->LoadModelForEntity(characterSingleton.modelEntity, modelHash);

        f32 width = 0.5f;
        f32 height = 1.6f;
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

        JPH::CharacterVirtualSettings characterSettings;
        characterSettings.mShape = shapeResult.Get();
        characterSettings.mBackFaceMode = JPH::EBackFaceMode::IgnoreBackFaces;

        characterSingleton.character = new JPH::CharacterVirtual(&characterSettings, JPH::RVec3(0.0f, 0.0f, 0.0f), JPH::Quat::sIdentity(), &joltState.physicsSystem);
        characterSingleton.character->SetShapeOffset(JPH::Vec3(0.0f, 0.0f, 0.0f));

        JPH::Vec3 newPosition = JPH::Vec3(-1500.0f, 310.0f, 1250.0f);

        MapLoader* mapLoader = ServiceLocator::GetGameRenderer()->GetMapLoader();
        if (mapLoader->GetCurrentMapID() == std::numeric_limits<u32>().max())
        {
            newPosition = JPH::Vec3(0.0f, 0.0f, 0.0f);
        }

        characterSingleton.character->SetPosition(newPosition);

        if (activeCamera.entity == orbitalCameraSettings.entity)
        {
            transformSystem.ParentEntityTo(characterSingleton.entity, orbitalCameraSettings.entity);
        }

        transformSystem.SetWorldPosition(characterSingleton.entity, vec3(newPosition.GetX(), newPosition.GetY(), newPosition.GetZ()));
    }
}