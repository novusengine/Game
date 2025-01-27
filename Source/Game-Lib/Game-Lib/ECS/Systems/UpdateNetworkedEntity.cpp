#include "UpdateNetworkedEntity.h"

#include "Game-Lib/ECS/Components/AnimationData.h"
#include "Game-Lib/ECS/Components/Model.h"
#include "Game-Lib/ECS/Components/MovementInfo.h"
#include "Game-Lib/ECS/Components/NetworkedEntity.h"
#include "Game-Lib/ECS/Components/UnitStatsComponent.h"
#include "Game-Lib/ECS/Singletons/CharacterSingleton.h"
#include "Game-Lib/ECS/Singletons/JoltState.h"
#include "Game-Lib/ECS/Util/Transforms.h"
#include "Game-Lib/Gameplay/Animation/Defines.h"
#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Rendering/Model/ModelLoader.h"
#include "Game-Lib/Util/AnimationUtil.h"
#include "Game-Lib/Util/ServiceLocator.h"
#include "Game-Lib/Util/UnitUtil.h"

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
        ZoneScopedN("ECS::UpdateNetworkedEntity");

        auto& characterSingleton = registry.ctx().get<Singletons::CharacterSingleton>();
        entt::entity characterEntity = characterSingleton.moverEntity;

        ModelLoader* modelLoader = ServiceLocator::GetGameRenderer()->GetModelLoader();

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

                    f32 timeToChange = 0.15f;
                    settings.z = timeToChange;
                };

                if (networkedEntity.bodyID != std::numeric_limits<u32>().max())
                {
                    Singletons::JoltState& joltState = registry.ctx().get<Singletons::JoltState>();

                    JPH::BodyID bodyID = JPH::BodyID(networkedEntity.bodyID);
                }

                if (!registry.all_of<Components::AnimationData>(entity))
                    return;

                if (networkedEntity.overrideAnimation == ::Animation::Defines::Type::Invalid)
                    ::Util::Unit::UpdateAnimationState(registry, entity, *model, deltaTime);

                bool isAlive = unitStatsComponent.currentHealth > 0.0f;
                if (isAlive)
                {
                    bool isMovingForward = movementInfo.movementFlags.forward;
                    bool isMovingBackward = movementInfo.movementFlags.backward;
                    bool isMovingLeft = movementInfo.movementFlags.left;
                    bool isMovingRight = movementInfo.movementFlags.right;
                    bool isGrounded = movementInfo.movementFlags.grounded;

                    const auto* modelInfo = modelLoader->GetModelInfo(model->modelHash);

                    if (isAlive && modelInfo && (isGrounded /* || (canControlInAir && isMoving))*/))
                    {
                        f32 spineOrientation = 0.0f;
                        f32 headOrientation = 0.0f;
                        f32 waistOrientation = 0.0f;

                        if (isMovingForward)
                        {
                            if (isMovingRight)
                            {
                                spineOrientation = -30.0f;
                                headOrientation = -30.0f;
                                waistOrientation = 45.0f;
                            }
                            else if (isMovingLeft)
                            {
                                spineOrientation = 30.0f;
                                headOrientation = 30.0f;
                                waistOrientation = -45.0f;
                            }
                        }
                        else if (isMovingBackward)
                        {
                            if (isMovingRight)
                            {
                                spineOrientation = 30.0f;
                                headOrientation = 15.0f;
                                waistOrientation = -45.0f;
                            }
                            else if (isMovingLeft)
                            {
                                spineOrientation = -30.0f;
                                headOrientation = -15.0f;
                                waistOrientation = 45.0f;
                            }
                        }
                        else if (isMovingRight)
                        {
                            spineOrientation = -45.0f;
                            headOrientation = -30.0f;
                            waistOrientation = 90.0f;
                        }
                        else if (isMovingLeft)
                        {
                            spineOrientation = 45.0f;
                            headOrientation = 30.0f;
                            waistOrientation = -90.0f;
                        }

                        SetOrientation(movementInfo.spineRotationSettings, spineOrientation);
                        SetOrientation(movementInfo.headRotationSettings, headOrientation);
                        SetOrientation(movementInfo.rootRotationSettings, waistOrientation);
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
                        auto& animationData = registry.get<Components::AnimationData>(entity);

                        quat rotation = glm::quat(glm::vec3(0.0f, glm::radians(movementInfo.spineRotationSettings.x), 0.0f));
                        Util::Animation::SetBoneRotation(modelInfo, animationData, ::Animation::Defines::Bone::SpineLow, rotation);
                    }
                    if (HandleUpdateOrientation(movementInfo.headRotationSettings, deltaTime))
                    {
                        auto& animationData = registry.get<Components::AnimationData>(entity);

                        quat rotation = glm::quat(glm::vec3(0.0f, glm::radians(movementInfo.headRotationSettings.x), 0.0f));
                        Util::Animation::SetBoneRotation(modelInfo, animationData, ::Animation::Defines::Bone::Head, rotation);
                    }
                    if (HandleUpdateOrientation(movementInfo.rootRotationSettings, deltaTime))
                    {
                        auto& animationData = registry.get<Components::AnimationData>(entity);

                        quat rotation = glm::quat(glm::vec3(0.0f, glm::radians(movementInfo.rootRotationSettings.x), 0.0f));
                        Util::Animation::SetBoneRotation(modelInfo, animationData, ::Animation::Defines::Bone::Default, rotation);
                    }
                }
            }
        });
    }
}