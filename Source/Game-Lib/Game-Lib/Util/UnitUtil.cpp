#include "UnitUtil.h"

#include "Game-Lib/Animation/AnimationSystem.h"
#include "Game-Lib/ECS/Components/CastInfo.h"
#include "Game-Lib/ECS/Components/Model.h"
#include "Game-Lib/ECS/Components/MovementInfo.h"
#include "Game-Lib/ECS/Components/UnitStatsComponent.h"
#include "Game-Lib/Util/AnimationUtil.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <FileFormat/Novus/ClientDB/Definitions.h>

#include <entt/entt.hpp>

using namespace ECS;

namespace Util::Unit
{
    bool PlayAnimation(::Animation::InstanceID instanceID, ::Animation::Bone bone, ::Animation::Type animationID, ::Animation::Flag flags, ::Animation::BlendOverride blendOverride, ::Animation::AnimationCallback callback)
    {
        ::Animation::AnimationSystem* animationSystem = ServiceLocator::GetAnimationSystem();

        if (!animationSystem->IsEnabled())
        {
            return false;
        }

        if (animationSystem->IsPlaying(instanceID, bone, animationID))
            return false;

        if (!animationSystem->SetBoneSequence(instanceID, bone, animationID, flags, blendOverride, callback))
            return false;

        return true;
    }

    bool UpdateAnimationState(entt::registry& registry, entt::entity entity, ::Animation::InstanceID instanceID, f32 deltaTime)
    {
        ::Animation::AnimationSystem* animationSystem = ServiceLocator::GetAnimationSystem();
        if (!animationSystem->IsEnabled())
        {
            return false;
        }

        if (instanceID == std::numeric_limits<u32>().max())
            return false;
        
        ::Animation::AnimationSequenceState currentAnimation = { };
        ::Animation::AnimationSequenceState nextAnimation = { };
        animationSystem->GetCurrentAnimation(instanceID, ::Animation::Bone::Default, &currentAnimation, &nextAnimation);

        const ClientDB::Definitions::AnimationData* currentAnimationData = Util::Animation::GetAnimationDataRec(registry, currentAnimation.animation);

        auto& unitStatsComponent = registry.get<Components::UnitStatsComponent>(entity);

        bool isAlive = unitStatsComponent.currentHealth > 0.0f;
        if (!isAlive)
        {
            return PlayAnimation(instanceID, ::Animation::Bone::Default, ::Animation::Type::Death, ::Animation::Flag::Freeze, ::Animation::BlendOverride::Start);
        }

        if (auto* castInfo = registry.try_get<Components::CastInfo>(entity))
        {
            if (currentAnimation.animation != ::Animation::Type::SpellCastDirected)
            {
                castInfo->duration = glm::min(castInfo->duration + 1.0f * deltaTime, castInfo->castTime);

                if (castInfo->castTime > 0.0f && (castInfo->castTime != castInfo->duration))
                {
                    return PlayAnimation(instanceID, ::Animation::Bone::Default, ::Animation::Type::ReadySpellDirected, ::Animation::Flag::Loop, ::Animation::BlendOverride::Start);
                }
                else if (castInfo->castTime == castInfo->duration)
                {
                    return PlayAnimation(instanceID, ::Animation::Bone::Default, ::Animation::Type::SpellCastDirected, ::Animation::Flag::Freeze, ::Animation::BlendOverride::Start);
                }
            }
            else
            {
                if ((u32)currentAnimation.flags & (u32)::Animation::Flag::Frozen)
                {
                    registry.erase<Components::CastInfo>(entity);
                }
            }

            return true;
        }

        auto& movementInfo = registry.get<Components::MovementInfo>(entity);

        bool isMovingForward = movementInfo.movementFlags.forward;
        bool isMovingBackward = movementInfo.movementFlags.backward;
        bool isMovingLeft = movementInfo.movementFlags.left;
        bool isMovingRight = movementInfo.movementFlags.right;
        bool isMoving = isMovingForward || isMovingBackward || isMovingLeft || isMovingRight;

        if (movementInfo.jumpState == Components::JumpState::Begin)
        {
            if (PlayAnimation(instanceID, ::Animation::Bone::Default, ::Animation::Type::JumpStart, ::Animation::Flag::Freeze, ::Animation::BlendOverride::Start))
            {
                movementInfo.jumpState = Components::JumpState::Jumping;
            }
        }
        
        if (movementInfo.jumpState == Components::JumpState::Jumping)
        {
            if (movementInfo.verticalVelocity > 0.0f)
                return true;

            movementInfo.jumpState = Components::JumpState::Fall;
        }

        if (!movementInfo.movementFlags.grounded)
        {
            if (currentAnimationData)
            {
                auto behaviorID = static_cast<::Animation::Type>(currentAnimationData->behaviorID);

                if (behaviorID >= ::Animation::Type::JumpStart && behaviorID <= ::Animation::Type::Fall)
                {
                    return true;
                }
            }

            return PlayAnimation(instanceID, ::Animation::Bone::Default, ::Animation::Type::Fall, ::Animation::Flag::Loop, ::Animation::BlendOverride::Start);
        }
        else if (isMovingBackward)
        {
            return PlayAnimation(instanceID, ::Animation::Bone::Default, ::Animation::Type::Walkbackwards, ::Animation::Flag::Loop, ::Animation::BlendOverride::Start);
        }
        else if (isMovingForward || isMovingLeft || isMovingRight)
        {
            if ((currentAnimation.animation == ::Animation::Type::JumpLandRun && ((u32)currentAnimation.flags & (u32)::Animation::Flag::Frozen) == 0) || nextAnimation.animation == ::Animation::Type::JumpLandRun)
            {
                return true;
            }

            if (movementInfo.movementFlags.justEndedJump)
            {
                return PlayAnimation(instanceID, ::Animation::Bone::Default, ::Animation::Type::JumpLandRun, ::Animation::Flag::Freeze, ::Animation::BlendOverride::Start);
            }

            ::Animation::Type animation = ::Animation::Type::Run;

            if (movementInfo.speed >= 11.0f)
            {
                animation = ::Animation::Type::Sprint;
            }

            return PlayAnimation(instanceID, ::Animation::Bone::Default, animation, ::Animation::Flag::Loop, ::Animation::BlendOverride::Start);
        }
        else
        {
            if ((currentAnimation.animation == ::Animation::Type::JumpEnd && ((u32)currentAnimation.flags & (u32)::Animation::Flag::Frozen) == 0) || nextAnimation.animation == ::Animation::Type::JumpEnd)
            {
                return true;
            }

            if (movementInfo.movementFlags.justEndedJump)
            {
                return PlayAnimation(instanceID, ::Animation::Bone::Default, ::Animation::Type::JumpEnd, ::Animation::Flag::Freeze, ::Animation::BlendOverride::Start);
            }

            return PlayAnimation(instanceID, ::Animation::Bone::Default, ::Animation::Type::Stand, ::Animation::Flag::Loop, ::Animation::BlendOverride::Start);
        }

        return true;
    }
}
