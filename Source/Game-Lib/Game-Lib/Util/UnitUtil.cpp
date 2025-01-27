#include "UnitUtil.h"

#include "Game-Lib/ECS/Components/AABB.h"
#include "Game-Lib/ECS/Components/AnimationData.h"
#include "Game-Lib/ECS/Components/AttachmentData.h"
#include "Game-Lib/ECS/Components/CastInfo.h"
#include "Game-Lib/ECS/Components/Model.h"
#include "Game-Lib/ECS/Components/Name.h"
#include "Game-Lib/ECS/Components/MovementInfo.h"
#include "Game-Lib/ECS/Components/UnitStatsComponent.h"
#include "Game-Lib/ECS/Util/Transforms.h"
#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Rendering/Model/ModelLoader.h"
#include "Game-Lib/Util/AnimationUtil.h"
#include "Game-Lib/Util/AttachmentUtil.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Base/Util/StringUtils.h>

#include <FileFormat/Novus/ClientDB/Definitions.h>

#include <entt/entt.hpp>

using namespace ECS;

namespace Util::Unit
{
    bool PlayAnimationRaw(const Model::ComplexModel* modelInfo, ::ECS::Components::AnimationData& animationData, u32 boneIndex, ::Animation::Defines::Type animationID, bool propagateToChildren, ::Animation::Defines::Flags flags, ::Animation::Defines::BlendOverride blendOverride, ::Animation::Defines::SequenceInterruptCallback callback)
    {
        u32 numBoneInstances = static_cast<u32>(animationData.boneInstances.size());
        if (boneIndex == ::Animation::Defines::InvalidBoneID || boneIndex >= numBoneInstances)
            return false;

        ::Animation::Defines::BoneInstance& animationBoneInstance = animationData.boneInstances[boneIndex];
        ::Animation::Defines::State& animationState = animationData.animationStates[animationBoneInstance.stateIndex];

        if (animationID != ::Animation::Defines::Type::Invalid && (animationState.currentAnimation == animationID || animationState.nextAnimation == animationID))
            return false;

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        if (!Animation::SetBoneSequenceRaw(*registry, modelInfo, animationData, boneIndex, animationID, propagateToChildren, flags, blendOverride))
            return false;

        return true;
    }

    bool PlayAnimation(const Model::ComplexModel* modelInfo, ::ECS::Components::AnimationData& animationData, ::Animation::Defines::Bone bone, ::Animation::Defines::Type animationID, bool propagateToChildren, ::Animation::Defines::Flags flags, ::Animation::Defines::BlendOverride blendOverride, ::Animation::Defines::SequenceInterruptCallback callback)
    {
        i16 boneIndex = Animation::GetBoneIndexFromKeyBoneID(modelInfo, bone);
        return PlayAnimationRaw(modelInfo, animationData, boneIndex, animationID, propagateToChildren, flags, blendOverride, callback);
    }

    bool UpdateAnimationState(entt::registry& registry, entt::entity entity, ::Components::Model& model, f32 deltaTime)
    {
        if (model.instanceID == std::numeric_limits<u32>().max())
            return false;

        ModelLoader* modelLoader = ServiceLocator::GetGameRenderer()->GetModelLoader();

        const auto* modelInfo = modelLoader->GetModelInfo(model.modelHash);
        if (!modelInfo)
            return false;

        i16 boneIndex = Animation::GetBoneIndexFromKeyBoneID(modelInfo, ::Animation::Defines::Bone::Default);
        if (boneIndex == ::Animation::Defines::InvalidBoneID)
            return false;

        auto& animationData = registry.get<Components::AnimationData>(entity);
        ::Animation::Defines::BoneInstance& animationBoneInstance = animationData.boneInstances[boneIndex];
        ::Animation::Defines::State& animationState = animationData.animationStates[animationBoneInstance.stateIndex];

        auto& unitStatsComponent = registry.get<Components::UnitStatsComponent>(entity);

        bool isAlive = unitStatsComponent.currentHealth > 0.0f;
        if (!isAlive)
        {
            return PlayAnimation(modelInfo, animationData, ::Animation::Defines::Bone::Default, ::Animation::Defines::Type::Death, false, ::Animation::Defines::Flags::HoldAtEnd, ::Animation::Defines::BlendOverride::Start);
        }

        if (auto* castInfo = registry.try_get<Components::CastInfo>(entity))
        {
            castInfo->duration = glm::min(castInfo->duration + 1.0f * deltaTime, castInfo->castTime);

            if (castInfo->castTime == castInfo->duration)
            {
                bool canPlaySpellCastDirected = Animation::HasAnimationSequence(registry, modelInfo, ::Animation::Defines::Type::SpellCastDirected);
                bool isPlayingSpellCastDirected = animationState.currentAnimation == ::Animation::Defines::Type::SpellCastDirected;

                if (canPlaySpellCastDirected && !isPlayingSpellCastDirected)
                {
                    return PlayAnimation(modelInfo, animationData, ::Animation::Defines::Bone::Default, ::Animation::Defines::Type::SpellCastDirected, false, ::Animation::Defines::Flags::HoldAtEnd, ::Animation::Defines::BlendOverride::Start);
                }
                else if (canPlaySpellCastDirected && isPlayingSpellCastDirected)
                {
                    if (::Animation::Defines::HasFlag(animationState.currentFlags, ::Animation::Defines::Flags::Finished))
                    {
                        registry.erase<Components::CastInfo>(entity);
                    }
                    else
                    {
                        return false;
                    }
                }
                else
                {
                    registry.erase<Components::CastInfo>(entity);
                }
            }
            else
            {
                bool canPlayReadySpellDirected = Animation::HasAnimationSequence(registry, modelInfo, ::Animation::Defines::Type::ReadySpellDirected);
                bool isPlayingReadySpellDirected = animationState.currentAnimation == ::Animation::Defines::Type::ReadySpellDirected;

                if (canPlayReadySpellDirected && !isPlayingReadySpellDirected)
                {
                    return PlayAnimation(modelInfo, animationData, ::Animation::Defines::Bone::Default, ::Animation::Defines::Type::ReadySpellDirected, false, ::Animation::Defines::Flags::HoldAtEnd, ::Animation::Defines::BlendOverride::Start);
                }

                return false;
            }
        }

        auto& movementInfo = registry.get<Components::MovementInfo>(entity);

        bool isMovingForward = movementInfo.movementFlags.forward;
        bool isMovingBackward = movementInfo.movementFlags.backward;
        bool isMovingLeft = movementInfo.movementFlags.left;
        bool isMovingRight = movementInfo.movementFlags.right;
        bool isMoving = isMovingForward || isMovingBackward || isMovingLeft || isMovingRight;

        if (movementInfo.jumpState == Components::JumpState::Begin)
        {
            if (PlayAnimation(modelInfo, animationData, ::Animation::Defines::Bone::Default, ::Animation::Defines::Type::JumpStart, false, ::Animation::Defines::Flags::HoldAtEnd, ::Animation::Defines::BlendOverride::None))
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
            //if (const ClientDB::Definitions::AnimationData* currentAnimationRecData = Util::Animation::Defines::GetAnimationDataRec(registry, animationState.currentAnimation))
            //{
            //    auto behaviorID = static_cast<::Animation::Defines::Type>(currentAnimationRecData->behaviorID);
            //
            //    if (behaviorID >= ::Animation::Defines::Type::JumpStart && behaviorID <= ::Animation::Defines::Type::Fall)
            //    {
            //        return true;
            //    }
            //}

            if ((animationState.currentAnimation == ::Animation::Defines::Type::JumpStart && !::Animation::Defines::HasFlag(animationState.currentFlags, ::Animation::Defines::Flags::Finished)) || animationState.nextAnimation == ::Animation::Defines::Type::JumpStart)
            {
                return true;
            }

            return PlayAnimation(modelInfo, animationData, ::Animation::Defines::Bone::Default, ::Animation::Defines::Type::Fall, false, ::Animation::Defines::Flags::None, ::Animation::Defines::BlendOverride::Start);
        }
        else if (isMovingBackward)
        {
            return PlayAnimation(modelInfo, animationData, ::Animation::Defines::Bone::Default, ::Animation::Defines::Type::Walkbackwards, false, ::Animation::Defines::Flags::None, ::Animation::Defines::BlendOverride::Start);
        }
        else if (isMovingForward || isMovingLeft || isMovingRight)
        {
            bool isInJumpLandRun = animationState.currentAnimation == ::Animation::Defines::Type::JumpLandRun;
            if ((isInJumpLandRun && !::Animation::Defines::HasFlag(animationState.currentFlags, ::Animation::Defines::Flags::Finished)) || animationState.nextAnimation == ::Animation::Defines::Type::JumpLandRun)
            {
                return true;
            }

            if (movementInfo.movementFlags.justEndedJump)
            {
                return PlayAnimation(modelInfo, animationData, ::Animation::Defines::Bone::Default, ::Animation::Defines::Type::JumpLandRun, false, ::Animation::Defines::Flags::HoldAtEnd, ::Animation::Defines::BlendOverride::None);
            }

            ::Animation::Defines::Type animation = ::Animation::Defines::Type::Run;

            if (movementInfo.speed >= 11.0f)
            {
                animation = ::Animation::Defines::Type::Sprint;
            }

            return PlayAnimation(modelInfo, animationData, ::Animation::Defines::Bone::Default, animation, false, ::Animation::Defines::Flags::None, ::Animation::Defines::BlendOverride::None);
        }
        else
        {
            if ((animationState.currentAnimation == ::Animation::Defines::Type::JumpEnd && !::Animation::Defines::HasFlag(animationState.currentFlags, ::Animation::Defines::Flags::Finished)) || animationState.nextAnimation == ::Animation::Defines::Type::JumpEnd)
            {
                return true;
            }

            if (movementInfo.movementFlags.justEndedJump)
            {
                return PlayAnimation(modelInfo, animationData, ::Animation::Defines::Bone::Default, ::Animation::Defines::Type::JumpEnd, false, ::Animation::Defines::Flags::HoldAtEnd, ::Animation::Defines::BlendOverride::Start);
            }

            return PlayAnimation(modelInfo, animationData, ::Animation::Defines::Bone::Default, ::Animation::Defines::Type::Stand, false, ::Animation::Defines::Flags::None, ::Animation::Defines::BlendOverride::Start);
        }

        return true;
    }

    bool IsHandClosed(entt::registry& registry, entt::entity entity, bool isLeftHand)
    {
        if (!registry.all_of<ECS::Components::AnimationData, ECS::Components::Model>(entity))
            return false;

        ModelLoader* modelLoader = ServiceLocator::GetGameRenderer()->GetModelLoader();
        auto& model = registry.get<ECS::Components::Model>(entity);
        auto& animationData = registry.get<ECS::Components::AnimationData>(entity);

        const auto* modelInfo = modelLoader->GetModelInfo(model.modelHash);
        if (!modelInfo)
            return false;

        u16 boneStartID = static_cast<u16>(::Animation::Defines::Bone::IndexFingerR) + (5 * isLeftHand);
        u16 boneEndID = boneStartID + 5;

        for (u16 i = boneStartID; i < boneEndID; i++)
        {
            ::Animation::Defines::Bone bone = static_cast<::Animation::Defines::Bone>(i);

            i16 boneIndex = Animation::GetBoneIndexFromKeyBoneID(modelInfo, bone);
            if (boneIndex == ::Animation::Defines::InvalidBoneID)
                continue;

            const ::Animation::Defines::BoneInstance& animationBoneInstance = animationData.boneInstances[boneIndex];
            const ::Animation::Defines::State& animationState = animationData.animationStates[animationBoneInstance.stateIndex];
            
            bool isHandsClosed = animationState.currentAnimation == ::Animation::Defines::Type::HandsClosed || animationState.nextAnimation == ::Animation::Defines::Type::HandsClosed;
            if (isHandsClosed)
                return true;
        }

        return false;
    }

    bool CloseHand(entt::registry& registry, entt::entity entity, bool isLeftHand)
    {
        if (!registry.all_of<ECS::Components::AnimationData, ECS::Components::Model>(entity))
            return false;

        ModelLoader* modelLoader = ServiceLocator::GetGameRenderer()->GetModelLoader();
        auto& model = registry.get<ECS::Components::Model>(entity);
        auto& animationData = registry.get<ECS::Components::AnimationData>(entity);

        const auto* modelInfo = modelLoader->GetModelInfo(model.modelHash);
        if (!modelInfo)
            return false;

        u16 boneIndex = static_cast<u16>(::Animation::Defines::Bone::IndexFingerR) + (5 * isLeftHand);
        u16 boneIndexMax = boneIndex + 5;

        bool didCloseFingers = false;
        for (u16 i = boneIndex; i < boneIndexMax; i++)
        {
            ::Animation::Defines::Bone bone = static_cast<::Animation::Defines::Bone>(i);
            didCloseFingers |= Util::Unit::PlayAnimation(modelInfo, animationData, bone, ::Animation::Defines::Type::HandsClosed, true, ::Animation::Defines::Flags::HoldAtEnd);
        }

        return didCloseFingers;
    }

    bool OpenHand(entt::registry& registry, entt::entity entity, bool isLeftHand)
    {
        if (!registry.all_of<ECS::Components::AnimationData, ECS::Components::Model>(entity))
            return false;

        ModelLoader* modelLoader = ServiceLocator::GetGameRenderer()->GetModelLoader();
        auto& model = registry.get<ECS::Components::Model>(entity);
        auto& animationData = registry.get<ECS::Components::AnimationData>(entity);

        const auto* modelInfo = modelLoader->GetModelInfo(model.modelHash);
        if (!modelInfo)
            return false;

        u16 boneIndex = static_cast<u16>(::Animation::Defines::Bone::IndexFingerR) + (5 * isLeftHand);
        u16 boneIndexMax = boneIndex + 5;

        bool didOpenFingers = false;
        for (u16 i = boneIndex; i < boneIndexMax; i++)
        {
            ::Animation::Defines::Bone bone = static_cast<::Animation::Defines::Bone>(i);
            didOpenFingers |= Util::Unit::PlayAnimation(modelInfo, animationData, bone, ::Animation::Defines::Type::Invalid, true, ::Animation::Defines::Flags::HoldAtEnd);
        }

        return didOpenFingers;
    }
    bool AddItemToHand(entt::registry& registry, entt::entity entity, ::Attachment::Defines::Type attachment, u32 displayID, entt::entity& itemEntity)
    {
        if (!registry.all_of<ECS::Components::AttachmentData, ECS::Components::Model>(entity))
            return false;

        auto& attachmentData = registry.get<ECS::Components::AttachmentData>(entity);
        auto& model = registry.get<ECS::Components::Model>(entity);

        if (!Attachment::EnableAttachment(entity, model, attachmentData, attachment))
            return false;

        entt::entity attachmentEntity = entt::null;
        if (!Attachment::GetAttachmentEntity(attachmentData, attachment, attachmentEntity))
            return false;

        entt::registry::context& ctx = registry.ctx();
        auto& transformSystem = ctx.get<TransformSystem>();

        // Release all previous children of this attachment point
        // TODO : Consider if we want a more dynamic/flexible system that allows for more than 1 entity using one of the equipment slot attachment points at the same time
        std::vector<entt::entity> attachedEntities;
        transformSystem.IterateChildren(attachmentEntity, [&registry, &attachedEntities](ECS::Components::SceneNode* childSceneNode)
        {
            entt::entity childEntity = childSceneNode->GetOwnerEntity();
            attachedEntities.push_back(childEntity);
        });

        for (entt::entity attachedEntity : attachedEntities)
        {
            registry.destroy(attachedEntity);
        }
        
        entt::entity attachedEntity = registry.create();
        auto& weaponName = registry.emplace<Components::Name>(attachedEntity);
        weaponName.name = "AttachedEntity";
        weaponName.fullName = "AttachedEntity";
        weaponName.nameHash = "AttachedEntity"_h;
        
        registry.emplace<Components::AABB>(attachedEntity);
        registry.emplace<Components::Transform>(attachedEntity);
        auto& attachedModel = registry.emplace<Components::Model>(attachedEntity);
        transformSystem.ParentEntityTo(attachmentEntity, attachedEntity);
        
        ModelLoader* modelLoader = ServiceLocator::GetGameRenderer()->GetModelLoader();
        if (!modelLoader->LoadDisplayIDForEntity(attachedEntity, attachedModel, ClientDB::Definitions::DisplayInfoType::Item, displayID))
        {
            // Force load cube?
            u32 modelHash = "spells/errorcube.complexmodel"_h;
            if (!modelLoader->LoadModelForEntity(attachedEntity, attachedModel, modelHash))
            {
                NC_LOG_ERROR("Util::Unit::AddItemToHand - Failed to load Item Display, then failed to load Error Cube!!!");
            }
        }
        
        itemEntity = attachedEntity;
        return true;
    }
}
