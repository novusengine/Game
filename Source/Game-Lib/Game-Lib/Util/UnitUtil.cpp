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
    bool PlayAnimation(::Animation::AnimationInstanceID instanceID, ::Animation::AnimationBone bone, ::Animation::AnimationType animationID, ::Animation::AnimationFlags flags, ::Animation::AnimationBlendOverride blendOverride, ::Animation::AnimationCallback callback)
    {
        /*::Animation::AnimationSystem* animationSystem = ServiceLocator::GetAnimationSystem();

        if (!animationSystem->IsEnabled())
        {
            return false;
        }

        if (animationSystem->IsPlaying(instanceID, bone, animationID))
            return false;

        if (!animationSystem->SetBoneSequence(instanceID, bone, animationID, flags, blendOverride, callback))
            return false;
        */
        return true;
    }

    bool UpdateAnimationState(entt::registry& registry, entt::entity entity, ::Animation::AnimationInstanceID instanceID, f32 deltaTime)
    {
        /*
        ::Animation::AnimationSystem* animationSystem = ServiceLocator::GetAnimationSystem();
        if (!animationSystem->IsEnabled())
        {
            return false;
        }

        if (instanceID == std::numeric_limits<u32>().max())
            return false;
        
        ::Animation::AnimationSequenceState currentAnimation = { };
        ::Animation::AnimationSequenceState nextAnimation = { };
        animationSystem->GetCurrentAnimation(instanceID, ::Animation::AnimationBone::Default, &currentAnimation, &nextAnimation);

        const ClientDB::Definitions::AnimationData* currentAnimationData = Util::Animation::GetAnimationDataRec(registry, currentAnimation.animation);

        auto& unitStatsComponent = registry.get<Components::UnitStatsComponent>(entity);

        bool isAlive = unitStatsComponent.currentHealth > 0.0f;
        if (!isAlive)
        {
            return PlayAnimation(instanceID, ::Animation::AnimationBone::Default, ::Animation::AnimationType::Death, ::Animation::AnimationFlags::HoldAtEnd, ::Animation::AnimationBlendOverride::Start);
        }

        if (auto* castInfo = registry.try_get<Components::CastInfo>(entity))
        {
            if (currentAnimation.animation != ::Animation::AnimationType::SpellCastDirected)
            {
                castInfo->duration = glm::min(castInfo->duration + 1.0f * deltaTime, castInfo->castTime);

                if (castInfo->castTime > 0.0f && (castInfo->castTime != castInfo->duration))
                {
                    return PlayAnimation(instanceID, ::Animation::AnimationBone::Default, ::Animation::AnimationType::ReadySpellDirected, ::Animation::AnimationFlags::None, ::Animation::AnimationBlendOverride::Start);
                }
                else if (castInfo->castTime == castInfo->duration)
                {
                    return PlayAnimation(instanceID, ::Animation::AnimationBone::Default, ::Animation::AnimationType::SpellCastDirected, ::Animation::AnimationFlags::HoldAtEnd, ::Animation::AnimationBlendOverride::Start);
                }
            }
            else
            {
                if ((u32)currentAnimation.flags & (u32)::Animation::AnimationFlags::Finished)
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
            if (PlayAnimation(instanceID, ::Animation::AnimationBone::Default, ::Animation::AnimationType::JumpStart, ::Animation::AnimationFlags::HoldAtEnd, ::Animation::AnimationBlendOverride::Start))
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
                auto behaviorID = static_cast<::Animation::AnimationType>(currentAnimationData->behaviorID);

                if (behaviorID >= ::Animation::AnimationType::JumpStart && behaviorID <= ::Animation::AnimationType::Fall)
                {
                    return true;
                }
            }

            return PlayAnimation(instanceID, ::Animation::AnimationBone::Default, ::Animation::AnimationType::Fall, ::Animation::AnimationFlags::None, ::Animation::AnimationBlendOverride::Start);
        }
        else if (isMovingBackward)
        {
            return PlayAnimation(instanceID, ::Animation::AnimationBone::Default, ::Animation::AnimationType::Walkbackwards, ::Animation::AnimationFlags::None, ::Animation::AnimationBlendOverride::Start);
        }
        else if (isMovingForward || isMovingLeft || isMovingRight)
        {
            if ((currentAnimation.animation == ::Animation::AnimationType::JumpLandRun && ((u32)currentAnimation.flags & (u32)::Animation::AnimationFlags::Finished) == 0) || nextAnimation.animation == ::Animation::AnimationType::JumpLandRun)
            {
                return true;
            }

            if (movementInfo.movementFlags.justEndedJump)
            {
                return PlayAnimation(instanceID, ::Animation::AnimationBone::Default, ::Animation::AnimationType::JumpLandRun, ::Animation::AnimationFlags::HoldAtEnd, ::Animation::AnimationBlendOverride::Start);
            }

            ::Animation::AnimationType animation = ::Animation::AnimationType::Run;

            if (movementInfo.speed >= 11.0f)
            {
                animation = ::Animation::AnimationType::Sprint;
            }

            return PlayAnimation(instanceID, ::Animation::AnimationBone::Default, animation, ::Animation::AnimationFlags::None, ::Animation::AnimationBlendOverride::Start);
        }
        else
        {
            if ((currentAnimation.animation == ::Animation::AnimationType::JumpEnd && ((u32)currentAnimation.flags & (u32)::Animation::AnimationFlags::Finished) == 0) || nextAnimation.animation == ::Animation::AnimationType::JumpEnd)
            {
                return true;
            }

            if (movementInfo.movementFlags.justEndedJump)
            {
                return PlayAnimation(instanceID, ::Animation::AnimationBone::Default, ::Animation::AnimationType::JumpEnd, ::Animation::AnimationFlags::HoldAtEnd, ::Animation::AnimationBlendOverride::Start);
            }

            return PlayAnimation(instanceID, ::Animation::AnimationBone::Default, ::Animation::AnimationType::Stand, ::Animation::AnimationFlags::None, ::Animation::AnimationBlendOverride::Start);
        }
        */
        return true;
    }
}
