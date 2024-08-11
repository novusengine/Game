#include "UpdateNetworkedEntity.h"

#include "Game/Animation/AnimationSystem.h"
#include "Game/ECS/Components/Model.h"
#include "Game/ECS/Components/MovementInfo.h"
#include "Game/ECS/Components/NetworkedEntity.h"
#include "Game/ECS/Components/UnitStatsComponent.h"
#include "Game/ECS/Singletons/CharacterSingleton.h"
#include "Game/ECS/Singletons/JoltState.h"
#include "Game/ECS/Util/Transforms.h"
#include "Game/Util/ServiceLocator.h"
#include "Game/Util/UnitUtil.h"

#include <entt/entt.hpp>
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/RayCast.h>

namespace ECS::Systems
{
    class NetworkedEntityFilter : public JPH::BodyFilter
    {
    public:
        NetworkedEntityFilter(u32 bodyID) : _bodyID(bodyID) { }

        bool ShouldCollide(const JPH::BodyID& inBodyID) const override
        {
            return _bodyID != inBodyID;
        }

        bool ShouldCollideLocked(const JPH::Body& inBody) const override
        {
            return _bodyID != inBody.GetID();
        }

    private:
        JPH::BodyID _bodyID;
    };

    void UpdateNetworkedEntity::Init(entt::registry& registry)
    {
    }

    void UpdateNetworkedEntity::Update(entt::registry& registry, f32 deltaTime)
    {
        auto& characterSingleton = registry.ctx().get<Singletons::CharacterSingleton>();
        entt::entity characterEntity = characterSingleton.moverEntity;

        auto view = registry.view<Components::Transform, Components::MovementInfo, Components::NetworkedEntity>();

        view.each([&, characterEntity](entt::entity entity, Components::Transform& transform, Components::MovementInfo& movementInfo, Components::NetworkedEntity& networkedEntity)
        {
            if (networkedEntity.positionProgress != -1.0f)
            {
                networkedEntity.positionProgress += 10.0f * deltaTime;

                vec3 initialPosition = networkedEntity.initialPosition;
                vec3 desiredPosition = networkedEntity.desiredPosition;
                f32 progress = glm::clamp(networkedEntity.positionProgress, 0.0f, 1.0f);
                vec3 newPosition = glm::mix(initialPosition, desiredPosition, progress);

                if (networkedEntity.bodyID != std::numeric_limits<u32>().max())
                {
                    Singletons::JoltState& joltState = registry.ctx().get<Singletons::JoltState>();

                    JPH::BodyID bodyID = JPH::BodyID(networkedEntity.bodyID);
                    quat rotation = transform.GetWorldRotation();

                    JPH::Vec3 start = JPH::Vec3(newPosition.x, newPosition.y, newPosition.z);
                    JPH::Vec3 direction = JPH::Vec3(0.0f, -2.0f, 0.0f);

                    JPH::RRayCast ray(start, direction);
                    JPH::RayCastResult hit;


                    bool isGrounded = movementInfo.movementFlags.grounded;
                    if (isGrounded)
                    {
                        if (movementInfo.movementFlags.jumping || movementInfo.jumpState != Components::JumpState::None)
                        {
                            movementInfo.jumpState = Components::JumpState::None;
                        }
                    }
                    else
                    {
                        bool isInJump = movementInfo.movementFlags.jumping;
                        if (isInJump)
                        {
                            if (movementInfo.jumpState == Components::JumpState::None)
                                movementInfo.jumpState = Components::JumpState::Begin;
                        }
                    }

                    joltState.physicsSystem.GetBodyInterface().SetPositionAndRotation(bodyID, JPH::Vec3(newPosition.x, newPosition.y, newPosition.z), JPH::Quat(rotation.x, rotation.y, rotation.z, rotation.w), JPH::EActivation::DontActivate);
                }

                TransformSystem& transformSystem = TransformSystem::Get(registry);
                transformSystem.SetWorldPosition(entity, newPosition);

                if (networkedEntity.positionProgress >= 1.0f)
                {
                    networkedEntity.positionProgress = -1.0f;
                }
            }

            Animation::AnimationSystem* animationSystem = ServiceLocator::GetAnimationSystem();
            if (auto* model = registry.try_get<Components::Model>(entity))
            {
                u32 instanceID = model->instanceID;
                auto& unitStatsComponent = registry.get<Components::UnitStatsComponent>(entity);

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

                if (networkedEntity.bodyID != std::numeric_limits<u32>().max())
                {
                    Singletons::JoltState& joltState = registry.ctx().get<Singletons::JoltState>();

                    JPH::BodyID bodyID = JPH::BodyID(networkedEntity.bodyID);
                }

                ::Util::Unit::UpdateAnimationState(registry, entity, instanceID, deltaTime);

                bool isAlive = unitStatsComponent.currentHealth > 0.0f;
                if (isAlive)
                {
                    bool isMovingForward = movementInfo.movementFlags.forward;
                    bool isMovingBackward = movementInfo.movementFlags.backward;
                    bool isMovingLeft = movementInfo.movementFlags.left;
                    bool isMovingRight = movementInfo.movementFlags.right;
                    bool isGrounded = movementInfo.movementFlags.grounded;

                    if (isAlive && (isGrounded /* || (canControlInAir && isMoving))*/))
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

                        SetOrientation(movementInfo.spineRotationSettings, spineOrientation);
                        SetOrientation(movementInfo.headRotationSettings, headOrientation);
                        SetOrientation(movementInfo.waistRotationSettings, waistOrientation);
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

                    if (HandleUpdateOrientation(movementInfo.spineRotationSettings, deltaTime))
                    {
                        quat rotation = glm::quat(glm::vec3(0.0f, glm::radians(movementInfo.spineRotationSettings.x), 0.0f));
                        animationSystem->SetBoneRotation(instanceID, Animation::Bone::SpineLow, rotation);
                    }
                    if (HandleUpdateOrientation(movementInfo.headRotationSettings, deltaTime))
                    {
                        quat rotation = glm::quat(glm::vec3(0.0f, glm::radians(movementInfo.headRotationSettings.x), 0.0f));
                        animationSystem->SetBoneRotation(instanceID, Animation::Bone::Head, rotation);
                    }
                    if (HandleUpdateOrientation(movementInfo.waistRotationSettings, deltaTime))
                    {
                        quat rotation = glm::quat(glm::vec3(0.0f, glm::radians(movementInfo.waistRotationSettings.x), 0.0f));
                        animationSystem->SetBoneRotation(instanceID, Animation::Bone::Waist, rotation);
                    }
                }
            }
        });
    }
}