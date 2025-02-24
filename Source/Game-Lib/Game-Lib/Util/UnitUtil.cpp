#include "UnitUtil.h"

#include "Game-Lib/ECS/Components/AABB.h"
#include "Game-Lib/ECS/Components/AnimationData.h"
#include "Game-Lib/ECS/Components/AttachmentData.h"
#include "Game-Lib/ECS/Components/CastInfo.h"
#include "Game-Lib/ECS/Components/Model.h"
#include "Game-Lib/ECS/Components/Name.h"
#include "Game-Lib/ECS/Components/MovementInfo.h"
#include "Game-Lib/ECS/Components/UnitEquipment.h"
#include "Game-Lib/ECS/Components/UnitStatsComponent.h"
#include "Game-Lib/ECS/Singletons/Database/ClientDBSingleton.h"
#include "Game-Lib/ECS/Singletons/Database/ItemSingleton.h"
#include "Game-Lib/ECS/Util/Transforms.h"
#include "Game-Lib/ECS/Util/Database/ItemUtil.h"
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
    bool PlayAnimationRaw(const Model::ComplexModel* modelInfo, Components::AnimationData& animationData, u32 boneIndex, ::Animation::Defines::Type animationID, bool propagateToChildren, ::Animation::Defines::Flags flags, ::Animation::Defines::BlendOverride blendOverride, f32 speedModifier, ::Animation::Defines::SequenceInterruptCallback callback)
    {
        u32 numBoneInstances = static_cast<u32>(animationData.boneInstances.size());
        if (boneIndex == ::Animation::Defines::InvalidBoneID || boneIndex >= numBoneInstances)
            return false;

        ::Animation::Defines::BoneInstance& animationBoneInstance = animationData.boneInstances[boneIndex];
        ::Animation::Defines::State& animationState = animationData.animationStates[animationBoneInstance.stateIndex];

        if (animationID != ::Animation::Defines::Type::Invalid && (animationState.currentAnimation == animationID || animationState.nextAnimation == animationID))
        {
            Animation::SetBoneSequenceSpeedModRaw(modelInfo, animationData, boneIndex, speedModifier);
            return false;
        }

        if (!Animation::SetBoneSequenceRaw(modelInfo, animationData, boneIndex, animationID, propagateToChildren, flags, blendOverride, speedModifier))
            return false;

        return true;
    }

    bool PlayAnimation(const Model::ComplexModel* modelInfo, Components::AnimationData& animationData, ::Animation::Defines::Bone bone, ::Animation::Defines::Type animationID, bool propagateToChildren, ::Animation::Defines::Flags flags, ::Animation::Defines::BlendOverride blendOverride, f32 speedModifier, ::Animation::Defines::SequenceInterruptCallback callback)
    {
        i16 boneIndex = Animation::GetBoneIndexFromKeyBoneID(modelInfo, bone);
        return PlayAnimationRaw(modelInfo, animationData, boneIndex, animationID, propagateToChildren, flags, blendOverride, speedModifier, callback);
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
                bool canPlaySpellCastDirected = Animation::HasAnimationSequence(modelInfo, ::Animation::Defines::Type::SpellCastDirected);
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
                bool canPlayReadySpellDirected = Animation::HasAnimationSequence(modelInfo, ::Animation::Defines::Type::ReadySpellDirected);
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
        bool isGrounded = movementInfo.movementFlags.grounded;
        bool isFlying = !isGrounded && movementInfo.movementFlags.flying;

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

        if (!isGrounded && !isFlying)
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
            ::Animation::Defines::Type animation = GetMoveBackwardAnimation(isFlying);
            auto blendOverride = isFlying ? ::Animation::Defines::BlendOverride::Auto : ::Animation::Defines::BlendOverride::Start;
            f32 speedModifier = glm::clamp(movementInfo.speed / 3.5555f, 0.5f, 1.0f);

            return PlayAnimation(modelInfo, animationData, ::Animation::Defines::Bone::Default, animation, false, ::Animation::Defines::Flags::None, blendOverride, speedModifier);
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

            f32 speed = movementInfo.speed;
            f32 baseSpeed = 4.0f;
            if (speed >= 11.0f)
            {
                baseSpeed = 11.0f;
            }
            else if (speed > 4.0f)
            {
                baseSpeed = 7.1111f;
            }

            f32 speedModifier = glm::clamp(movementInfo.speed / baseSpeed, 0.1f, 1.5f);
            bool isStealthed = false;

            if (isMovingLeft)
            {
                animation = GetMoveLeftAnimation(speed, isFlying, isStealthed);
            }
            else if (isMovingRight)
            {
                animation = GetMoveRightAnimation(speed, isFlying, isStealthed);
            }
            else  if (isMovingForward)
            {
                animation = GetMoveForwardAnimation(speed, isFlying, isStealthed);
            }

            auto blendOverride = isFlying ? ::Animation::Defines::BlendOverride::Auto : ::Animation::Defines::BlendOverride::Start;
            return PlayAnimation(modelInfo, animationData, ::Animation::Defines::Bone::Default, animation, false, ::Animation::Defines::Flags::None, blendOverride, speedModifier);
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

            bool isStealthed = false;
            ::Animation::Defines::Type animation = GetIdleAnimation(isFlying, isStealthed);
            auto blendOverride = isFlying ? ::Animation::Defines::BlendOverride::Auto : ::Animation::Defines::BlendOverride::Start;
            return PlayAnimation(modelInfo, animationData, ::Animation::Defines::Bone::Default, animation, false, ::Animation::Defines::Flags::None, blendOverride);
        }

        return true;
    }

    bool IsHandClosed(entt::registry& registry, entt::entity entity, bool isOffHand)
    {
        if (!registry.all_of<Components::AnimationData, Components::Model>(entity))
            return false;

        ModelLoader* modelLoader = ServiceLocator::GetGameRenderer()->GetModelLoader();
        auto& model = registry.get<Components::Model>(entity);
        auto& animationData = registry.get<Components::AnimationData>(entity);

        const auto* modelInfo = modelLoader->GetModelInfo(model.modelHash);
        if (!modelInfo)
            return false;

        u16 boneStartID = static_cast<u16>(::Animation::Defines::Bone::IndexFingerR) + (5 * isOffHand);
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

    bool CloseHand(entt::registry& registry, entt::entity entity, bool isOffHand)
    {
        if (!registry.all_of<Components::AnimationData, Components::Model>(entity))
            return false;

        ModelLoader* modelLoader = ServiceLocator::GetGameRenderer()->GetModelLoader();
        auto& model = registry.get<Components::Model>(entity);
        auto& animationData = registry.get<Components::AnimationData>(entity);

        const auto* modelInfo = modelLoader->GetModelInfo(model.modelHash);
        if (!modelInfo)
            return false;

        u16 boneIndex = static_cast<u16>(::Animation::Defines::Bone::IndexFingerR) + (5 * isOffHand);
        u16 boneIndexMax = boneIndex + 5;

        bool didCloseFingers = false;
        for (u16 i = boneIndex; i < boneIndexMax; i++)
        {
            ::Animation::Defines::Bone bone = static_cast<::Animation::Defines::Bone>(i);
            didCloseFingers |= ::Util::Unit::PlayAnimation(modelInfo, animationData, bone, ::Animation::Defines::Type::HandsClosed, true, ::Animation::Defines::Flags::HoldAtEnd);
        }

        return didCloseFingers;
    }

    bool OpenHand(entt::registry& registry, entt::entity entity, bool isOffHand)
    {
        if (!registry.all_of<Components::AnimationData, Components::Model>(entity))
            return false;

        ModelLoader* modelLoader = ServiceLocator::GetGameRenderer()->GetModelLoader();
        auto& model = registry.get<Components::Model>(entity);
        auto& animationData = registry.get<Components::AnimationData>(entity);

        const auto* modelInfo = modelLoader->GetModelInfo(model.modelHash);
        if (!modelInfo)
            return false;

        u16 boneIndex = static_cast<u16>(::Animation::Defines::Bone::IndexFingerR) + (5 * isOffHand);
        u16 boneIndexMax = boneIndex + 5;

        bool didOpenFingers = false;
        for (u16 i = boneIndex; i < boneIndexMax; i++)
        {
            ::Animation::Defines::Bone bone = static_cast<::Animation::Defines::Bone>(i);
            didOpenFingers |= ::Util::Unit::PlayAnimation(modelInfo, animationData, bone, ::Animation::Defines::Type::Invalid, true, ::Animation::Defines::Flags::HoldAtEnd);
        }

        return didOpenFingers;
    }

    bool AddHelm(entt::registry& registry, const entt::entity entity, const Database::Item::Item& item, GameDefine::UnitRace race, GameDefine::Gender gender, entt::entity& itemEntity)
    {
        itemEntity = entt::null;

        if (!registry.all_of<Components::AttachmentData, Components::Model>(entity))
            return false;

        entt::registry* dbRegistry = ServiceLocator::GetEnttRegistries()->dbRegistry;
        auto& clientDBSingleton = dbRegistry->ctx().get<Singletons::Database::ClientDBSingleton>();
        auto& itemSingleton = dbRegistry->ctx().get<Singletons::Database::ItemSingleton>();
        auto* itemDisplayInfoStorage = clientDBSingleton.Get(ClientDBHash::ItemDisplayInfo);

        if (item.displayID == 0 || !itemDisplayInfoStorage->Has(item.displayID))
            return false;

        auto& itemDisplayInfo = itemDisplayInfoStorage->Get<ClientDB::Definitions::ItemDisplayInfo>(item.displayID);
        u32 modelResourcesID = itemDisplayInfo.modelResourcesID[0];

        if (!itemSingleton.helmModelResourcesIDToModelMapping.contains(modelResourcesID))
            return false;

        auto* itemArmorTemplateStorage = clientDBSingleton.Get(ClientDBHash::ItemArmorTemplate);
        bool hasArmorTemplate = item.armorTemplateID != 0 && itemArmorTemplateStorage->Has(item.armorTemplateID);
        if (!hasArmorTemplate)
            return false;

        auto& armorTemplate = itemArmorTemplateStorage->Get<Database::Item::ItemArmorTemplate>(item.armorTemplateID);
        if (armorTemplate.equipType != Database::Item::ItemArmorEquipType::Helm)
            return false;

        u8 helmVariant = 0;
        u32 itemModelHash = ECS::Util::Database::Item::GetModelHashForHelm(itemSingleton, modelResourcesID, race, gender, helmVariant);
        return AddItemToAttachment(registry, entity, ::Attachment::Defines::Type::Helm, item.displayID, itemEntity, itemModelHash, helmVariant);
    }

    bool AddShoulders(entt::registry& registry, const entt::entity entity, const Database::Item::Item& item, entt::entity& shoulderLeftEntity, entt::entity& shoulderRightEntity)
    {
        shoulderLeftEntity = entt::null;
        shoulderRightEntity = entt::null;

        if (!registry.all_of<Components::AttachmentData, Components::Model>(entity))
            return false;

        entt::registry* dbRegistry = ServiceLocator::GetEnttRegistries()->dbRegistry;
        auto& clientDBSingleton = dbRegistry->ctx().get<Singletons::Database::ClientDBSingleton>();
        auto& itemSingleton = dbRegistry->ctx().get<Singletons::Database::ItemSingleton>();
        auto* itemDisplayInfoStorage = clientDBSingleton.Get(ClientDBHash::ItemDisplayInfo);

        if (item.displayID == 0 || !itemDisplayInfoStorage->Has(item.displayID))
            return false;

        auto& itemDisplayInfo = itemDisplayInfoStorage->Get<ClientDB::Definitions::ItemDisplayInfo>(item.displayID);
        u32 modelResourcesID = itemDisplayInfo.modelResourcesID[0];

        if (!itemSingleton.shoulderModelResourcesIDToModelMapping.contains(modelResourcesID))
            return false;

        auto* itemArmorTemplateStorage = clientDBSingleton.Get(ClientDBHash::ItemArmorTemplate);
        bool hasArmorTemplate = item.armorTemplateID != 0 && itemArmorTemplateStorage->Has(item.armorTemplateID);
        if (!hasArmorTemplate)
            return false;

        auto& armorTemplate = itemArmorTemplateStorage->Get<Database::Item::ItemArmorTemplate>(item.armorTemplateID);
        if (armorTemplate.equipType != Database::Item::ItemArmorEquipType::Shoulders)
            return false;

        u32 shoulderLeftModelHash;
        u32 shoulderRightModelHash;
        ECS::Util::Database::Item::GetModelHashesForShoulders(itemSingleton, modelResourcesID, shoulderLeftModelHash, shoulderRightModelHash);

        bool hasLeftShoulderModel = shoulderLeftModelHash != std::numeric_limits<u32>().max();
        bool addedLeftShoulder = false;
        if (hasLeftShoulderModel)
        {
            addedLeftShoulder = AddItemToAttachment(registry, entity, ::Attachment::Defines::Type::ShoulderLeft, item.displayID, shoulderLeftEntity, shoulderLeftModelHash);
        }

        bool hasRightShoulderModel = shoulderRightModelHash != std::numeric_limits<u32>().max();
        bool addedRightShoulder = false;
        if (hasRightShoulderModel)
        {
            addedRightShoulder = AddItemToAttachment(registry, entity, ::Attachment::Defines::Type::ShoulderRight, item.displayID, shoulderRightEntity, shoulderRightModelHash, 1);
        }

        bool result = (hasLeftShoulderModel || hasRightShoulderModel) && (!hasLeftShoulderModel || (hasLeftShoulderModel && addedLeftShoulder)) && (!hasRightShoulderModel || (hasRightShoulderModel && addedRightShoulder));
        return result;
    }

    bool AddWeaponToHand(entt::registry& registry, const entt::entity entity, const Database::Item::Item& item, const bool isOffHand, entt::entity& itemEntity)
    {
        itemEntity = entt::null;
            
        if (!registry.all_of<Components::AttachmentData, Components::Model>(entity))
            return false;

        entt::registry* dbRegistry = ServiceLocator::GetEnttRegistries()->dbRegistry;
        auto& clientDBSingleton = dbRegistry->ctx().get<Singletons::Database::ClientDBSingleton>();
        auto* itemDisplayInfoStorage = clientDBSingleton.Get(ClientDBHash::ItemDisplayInfo);

        if (item.displayID == 0 || !itemDisplayInfoStorage->Has(item.displayID))
            return false;

        auto* itemWeaponTemplateStorage = clientDBSingleton.Get(ClientDBHash::ItemWeaponTemplate);
        auto* itemShieldTemplateStorage = clientDBSingleton.Get(ClientDBHash::ItemShieldTemplate);

        bool hasWeaponTemplate = item.weaponTemplateID != 0 && itemWeaponTemplateStorage->Has(item.weaponTemplateID);
        bool hasShieldTemplate = item.shieldTemplateID != 0 && itemShieldTemplateStorage->Has(item.shieldTemplateID);

        if (!hasWeaponTemplate && !hasShieldTemplate)
            return false;

        ::Attachment::Defines::Type attachmentType = ::Attachment::Defines::Type::Invalid;

        if (hasWeaponTemplate)
        {
            auto& itemWeaponTemplate = itemWeaponTemplateStorage->Get<Database::Item::ItemWeaponTemplate>(item.weaponTemplateID);
            Database::Item::ItemWeaponStyle weaponStyle = itemWeaponTemplate.weaponStyle;

            bool canWearInOffHand = weaponStyle == Database::Item::ItemWeaponStyle::Unspecified || weaponStyle == Database::Item::ItemWeaponStyle::OneHand || 
                                    weaponStyle == Database::Item::ItemWeaponStyle::OffHand     || weaponStyle == Database::Item::ItemWeaponStyle::Tool;

            if (isOffHand && !canWearInOffHand)
                return false;
            
            attachmentType = (isOffHand && canWearInOffHand) ? ::Attachment::Defines::Type::HandLeft : ::Attachment::Defines::Type::HandRight;

            if (attachmentType == ::Attachment::Defines::Type::HandLeft)
                ::Util::Unit::RemoveItemFromAttachment(registry, entity, ::Attachment::Defines::Type::Shield);
        }
        else if (hasShieldTemplate)
        {
            ::Util::Unit::RemoveItemFromAttachment(registry, entity, ::Attachment::Defines::Type::HandLeft);
            attachmentType = ::Attachment::Defines::Type::Shield;
        }

        return AddItemToAttachment(registry, entity, attachmentType, item.displayID, itemEntity);
    }

    bool AddItemToAttachment(entt::registry& registry, entt::entity entity, ::Attachment::Defines::Type attachment, u32 displayID, entt::entity& itemEntity, u32 modelHash, u8 modelVariant)
    {
        if (!registry.all_of<Components::AttachmentData, Components::Model>(entity))
            return false;

        auto& attachmentData = registry.get<Components::AttachmentData>(entity);
        auto& model = registry.get<Components::Model>(entity);

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
        transformSystem.IterateChildren(attachmentEntity, [&registry, &attachedEntities](Components::SceneNode* childSceneNode)
        {
            entt::entity childEntity = childSceneNode->GetOwnerEntity();
            attachedEntities.push_back(childEntity);
        });

        ModelLoader* modelLoader = ServiceLocator::GetGameRenderer()->GetModelLoader();
        for (entt::entity attachedEntity : attachedEntities)
        {
            auto& attachedModel = registry.get<Components::Model>(attachedEntity);
            modelLoader->UnloadModelForEntity(attachedEntity, attachedModel);

            transformSystem.ClearParent(attachedEntity);
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
        
        if (!modelLoader->LoadDisplayIDForEntity(attachedEntity, attachedModel, ClientDB::Definitions::DisplayInfoType::Item, displayID, modelHash, modelVariant))
        {
            // Force load cube?
            u32 modelHash = "spells/errorcube.complexmodel"_h;
            if (!modelLoader->LoadModelForEntity(attachedEntity, attachedModel, modelHash))
            {
                NC_LOG_ERROR("Util::Unit::AddItemToAttachment - Failed to load Item Display, then failed to load Error Cube!!!");
            }
        }
        
        itemEntity = attachedEntity;
        return true;
    }
    bool RemoveItemFromAttachment(entt::registry& registry, entt::entity entity, ::Attachment::Defines::Type attachment)
    {
        if (!registry.all_of<Components::AttachmentData, Components::Model>(entity))
            return false;

        auto& attachmentData = registry.get<Components::AttachmentData>(entity);
        auto& model = registry.get<Components::Model>(entity);

        entt::entity attachmentEntity = entt::null;
        if (!Attachment::GetAttachmentEntity(attachmentData, attachment, attachmentEntity))
            return false;

        entt::registry::context& ctx = registry.ctx();
        auto& transformSystem = ctx.get<TransformSystem>();

        // Release all previous children of this attachment point
        // TODO : Consider if we want a more dynamic/flexible system that allows for more than 1 entity using one of the equipment slot attachment points at the same time
        std::vector<entt::entity> attachedEntities;
        transformSystem.IterateChildren(attachmentEntity, [&registry, &attachedEntities](Components::SceneNode* childSceneNode)
        {
            entt::entity childEntity = childSceneNode->GetOwnerEntity();
            attachedEntities.push_back(childEntity);
        });

        ModelLoader* modelLoader = ServiceLocator::GetGameRenderer()->GetModelLoader();
        for (entt::entity attachedEntity : attachedEntities)
        {
            auto& attachedModel = registry.get<Components::Model>(attachedEntity);
            modelLoader->UnloadModelForEntity(attachedEntity, attachedModel);

            registry.destroy(attachedEntity);
        }

        return true;
    }

    void EnableGeometryGroup(entt::registry& registry, entt::entity entity, ::ECS::Components::Model& model, u32 groupID)
    {
        if (model.flags.loaded)
        {
            auto* modelLoader = ServiceLocator::GetGameRenderer()->GetModelLoader();
            modelLoader->EnableGroupForModel(model, groupID);
        }
        else
        {
            auto& modelQueuedGeometryGroups = registry.get_or_emplace<Components::ModelQueuedGeometryGroups>(entity);
            modelQueuedGeometryGroups.enabledGroupIDs.insert(groupID);
        }
    }

    void DisableGeometryGroups(entt::registry& registry, entt::entity entity, ::ECS::Components::Model& model, u32 startGroupID, u32 endGroupID)
    {
        if (model.flags.loaded)
        {
            auto* modelLoader = ServiceLocator::GetGameRenderer()->GetModelLoader();
            modelLoader->DisableGroupsForModel(model, startGroupID, endGroupID);
        }
        else
        {
            auto& modelQueuedGeometryGroups = registry.get_or_emplace<Components::ModelQueuedGeometryGroups>(entity);

            if (endGroupID == 0)
            {
                modelQueuedGeometryGroups.enabledGroupIDs.erase(startGroupID);
            }
            else
            {
                for (auto it = modelQueuedGeometryGroups.enabledGroupIDs.begin(); it != modelQueuedGeometryGroups.enabledGroupIDs.end();)
                {
                    u32 groupID = *it;
                    if (groupID >= startGroupID && groupID <= endGroupID)
                    {
                        it = modelQueuedGeometryGroups.enabledGroupIDs.erase(it);
                    }
                    else
                    {
                        ++it;
                    }
                }
            }
        }
    }

    void DisableAllGeometryGroups(entt::registry& registry, entt::entity entity, ::ECS::Components::Model& model)
    {
        if (model.flags.loaded)
        {
            auto* modelLoader = ServiceLocator::GetGameRenderer()->GetModelLoader();
            modelLoader->DisableAllGroupsForModel(model);
        }
        else
        {
            auto& modelQueuedGeometryGroups = registry.get_or_emplace<Components::ModelQueuedGeometryGroups>(entity);
            modelQueuedGeometryGroups.enabledGroupIDs.clear();
        }
    }

    void RefreshGeometryGroups(entt::registry& registry, entt::entity entity, ::ECS::Components::Model& model)
    {
        auto& clientDBSingleton = ServiceLocator::GetEnttRegistries()->dbRegistry->ctx().get<Singletons::Database::ClientDBSingleton>();
        auto* itemStorage = clientDBSingleton.Get(ClientDBHash::Item);
        auto* itemDisplayInfoStorage = clientDBSingleton.Get(ClientDBHash::ItemDisplayInfo);

        auto& unitEquipment = registry.get<::ECS::Components::UnitEquipment>(entity);

        auto GetItemDisplayID = [&itemStorage, &itemDisplayInfoStorage, &unitEquipment](Database::Item::ItemEquipSlot equipSlot) -> u32
        {
            u32 itemID = unitEquipment.equipmentSlotToItemID[(u32)equipSlot];
            if (itemID == 0)
                return 0;

            const auto& itemInfo = itemStorage->Get<::Database::Item::Item>(itemID);
            return itemInfo.displayID;
        };

        u32 shirtDisplayID = GetItemDisplayID(Database::Item::ItemEquipSlot::Shirt);
        u32 chestDisplayID = GetItemDisplayID(Database::Item::ItemEquipSlot::Chest);
        u32 beltDisplayID = GetItemDisplayID(Database::Item::ItemEquipSlot::Belt);
        u32 pantsDisplayID = GetItemDisplayID(Database::Item::ItemEquipSlot::Pants);
        u32 bootsDisplayID = GetItemDisplayID(Database::Item::ItemEquipSlot::Boots);
        u32 glovesDisplayID = GetItemDisplayID(Database::Item::ItemEquipSlot::Gloves);
        u32 tabardDisplayID = GetItemDisplayID(Database::Item::ItemEquipSlot::Tabard);
        u32 cloakDisplayID = GetItemDisplayID(Database::Item::ItemEquipSlot::Cloak);

        // Default Geometry
        ::Util::Unit::EnableGeometryGroup(registry, entity, model, 1);
        ::Util::Unit::EnableGeometryGroup(registry, entity, model, 101);
        ::Util::Unit::EnableGeometryGroup(registry, entity, model, 201);
        ::Util::Unit::EnableGeometryGroup(registry, entity, model, 301);
        ::Util::Unit::EnableGeometryGroup(registry, entity, model, 401);
        ::Util::Unit::EnableGeometryGroup(registry, entity, model, 501);
        ::Util::Unit::EnableGeometryGroup(registry, entity, model, 702);
        ::Util::Unit::EnableGeometryGroup(registry, entity, model, 1101);
        ::Util::Unit::EnableGeometryGroup(registry, entity, model, 1301);
        ::Util::Unit::EnableGeometryGroup(registry, entity, model, 1501);

        if (glovesDisplayID > 0)
        {
            auto* glovesDisplayInfo = itemDisplayInfoStorage->TryGet<ClientDB::Definitions::ItemDisplayInfo>(glovesDisplayID);
            if (glovesDisplayInfo && glovesDisplayInfo->goesetGroup[0])
            {
                DisableGeometryGroups(registry, entity, model, 401, 499);

                u32 geometryGroupID = 401 + glovesDisplayInfo->goesetGroup[0];
                EnableGeometryGroup(registry, entity, model, geometryGroupID);
            }
        }
        else if (chestDisplayID > 0)
        {
            auto* chestDisplayInfo = itemDisplayInfoStorage->TryGet<ClientDB::Definitions::ItemDisplayInfo>(chestDisplayID);
            if (chestDisplayInfo && chestDisplayInfo->goesetGroup[0])
            {
                u32 geometryGroupID = 801 + chestDisplayInfo->goesetGroup[0];
                EnableGeometryGroup(registry, entity, model, geometryGroupID);
            }
        }

        /* Shirt Stuff here */

        auto* tabardDisplayInfo = itemDisplayInfoStorage->TryGet<ClientDB::Definitions::ItemDisplayInfo>(tabardDisplayID);
        if (tabardDisplayInfo)
        {
            if (!(tabardDisplayInfo->flags & 0x100000))
            {
                DisableGeometryGroups(registry, entity, model, 2200, 2299);
                EnableGeometryGroup(registry, entity, model, 2202);
            }
        }
        else if (chestDisplayID > 0)
        {
            auto* chestDisplayInfo = itemDisplayInfoStorage->TryGet<ClientDB::Definitions::ItemDisplayInfo>(chestDisplayID);
            if (chestDisplayInfo && chestDisplayInfo->goesetGroup[3])
            {
                u32 geometryGroupID = 2201 + chestDisplayInfo->goesetGroup[3];
                DisableGeometryGroups(registry, entity, model, 2200, 2299);
                EnableGeometryGroup(registry, entity, model, geometryGroupID);
            }
        }
        
        bool hasBeltModel = false;
        bool hasDressPants = false;
        bool hasDressChestPiece = false;

        auto* beltDisplayInfo = itemDisplayInfoStorage->TryGet<ClientDB::Definitions::ItemDisplayInfo>(beltDisplayID);
        if (beltDisplayInfo)
            hasBeltModel = beltDisplayInfo->flags & 0x200;

        if (chestDisplayID > 0)
        {
            auto* chestDisplayInfo = itemDisplayInfoStorage->TryGet<ClientDB::Definitions::ItemDisplayInfo>(chestDisplayID);
            if (chestDisplayInfo && chestDisplayInfo->goesetGroup[2])
            {
                hasDressPants = false;
                hasDressChestPiece = true;

                DisableGeometryGroups(registry, entity, model, 501, 599);
                DisableGeometryGroups(registry, entity, model, 902, 999);
                DisableGeometryGroups(registry, entity, model, 1100, 1199);
                DisableGeometryGroups(registry, entity, model, 1300, 1399);

                u32 geometryGroupID = 1301 + chestDisplayInfo->goesetGroup[2];
                EnableGeometryGroup(registry, entity, model, geometryGroupID);
            }
        }
        else if (pantsDisplayID > 0)
        {
            auto* pantsDisplayInfo = itemDisplayInfoStorage->TryGet<ClientDB::Definitions::ItemDisplayInfo>(pantsDisplayID);
            if (pantsDisplayInfo && pantsDisplayInfo->goesetGroup[2])
            {
                hasDressPants = true;
                hasDressChestPiece = false;

                DisableGeometryGroups(registry, entity, model, 501, 599);
                DisableGeometryGroups(registry, entity, model, 902, 999);
                DisableGeometryGroups(registry, entity, model, 1100, 1199);
                DisableGeometryGroups(registry, entity, model, 1300, 1399);

                u32 geometryGroupID = 1301 + pantsDisplayInfo->goesetGroup[2];
                EnableGeometryGroup(registry, entity, model, geometryGroupID);
            }
        }

        if (!hasDressPants && !hasDressChestPiece)
        {
            hasDressPants = false;
            hasDressChestPiece = false;

            auto* bootsDisplayInfo = itemDisplayInfoStorage->TryGet<ClientDB::Definitions::ItemDisplayInfo>(bootsDisplayID);
            if (bootsDisplayInfo && bootsDisplayInfo->goesetGroup[0])
            {
                DisableGeometryGroups(registry, entity, model, 501, 599);
                EnableGeometryGroup(registry, entity, model, 901);

                u32 geometryGroupID = 501 + bootsDisplayInfo->goesetGroup[0];
                EnableGeometryGroup(registry, entity, model, geometryGroupID);
            }
            else
            {
                auto* pantsDisplayInfo = itemDisplayInfoStorage->TryGet<ClientDB::Definitions::ItemDisplayInfo>(pantsDisplayID);
                u32 geometryGroupID = pantsDisplayInfo && pantsDisplayInfo->goesetGroup[1] ? 901 + pantsDisplayInfo->goesetGroup[1] : 901;
                EnableGeometryGroup(registry, entity, model, geometryGroupID);
            }
        }

        // Boots
        {
            bool bootsFlag = false;
            auto* bootsDisplayInfo = itemDisplayInfoStorage->TryGet<ClientDB::Definitions::ItemDisplayInfo>(bootsDisplayID);
            if (bootsDisplayInfo)
                bootsFlag = (bootsDisplayInfo->flags & 0x100000) == 0;

            u32 bootsGeometryGroupID = 0;
            if (bootsDisplayInfo && bootsDisplayInfo->goesetGroup[1])
            {
                bootsGeometryGroupID = 2000 + bootsDisplayInfo->goesetGroup[1];
            }
            else
            {
                bootsGeometryGroupID = bootsFlag ? 2002 : 2001;
            }
            EnableGeometryGroup(registry, entity, model, bootsGeometryGroupID);
        }

        bool showsTabard = false;
        bool hasDress = hasDressChestPiece || hasDressPants;

        if (!hasDress && tabardDisplayID > 0 && tabardDisplayInfo && tabardDisplayInfo->goesetGroup[0])
        {
            showsTabard = true;
            u32 geometryGroupID = hasBeltModel ? 1203 : 1201 + tabardDisplayInfo->goesetGroup[0];
            EnableGeometryGroup(registry, entity, model, geometryGroupID);
        }
        else if (tabardDisplayID > 0)
        {
            u32 geometryGroupID1 = 1201;
            EnableGeometryGroup(registry, entity, model, geometryGroupID1);

            if (!hasDress)
            {
                showsTabard = true;

                u32 geometryGroupID2 = 1202;
                EnableGeometryGroup(registry, entity, model, geometryGroupID2);
            }
        }

        if (!hasDressChestPiece && !showsTabard)
        {
            if (chestDisplayID)
            {
                auto* chestDisplayInfo = itemDisplayInfoStorage->TryGet<ClientDB::Definitions::ItemDisplayInfo>(chestDisplayID);
                if (chestDisplayInfo && chestDisplayInfo->goesetGroup[1])
                {
                    u32 geometryGroupID = 1001 + chestDisplayInfo->goesetGroup[1];
                    EnableGeometryGroup(registry, entity, model, geometryGroupID);
                }
            }
            else if (shirtDisplayID)
            {
                auto* shirtDisplayInfo = itemDisplayInfoStorage->TryGet<ClientDB::Definitions::ItemDisplayInfo>(shirtDisplayID);
                if (shirtDisplayInfo && shirtDisplayInfo->goesetGroup[1])
                {
                    u32 geometryGroupID = 1001 + shirtDisplayInfo->goesetGroup[1];
                    EnableGeometryGroup(registry, entity, model, geometryGroupID);
                }
            }
        }

        if (!hasDressChestPiece)
        {
            if (pantsDisplayID)
            {
                auto* pantsDisplayInfo = itemDisplayInfoStorage->TryGet<ClientDB::Definitions::ItemDisplayInfo>(pantsDisplayID);
                if (pantsDisplayInfo && pantsDisplayInfo->goesetGroup[0])
                {
                    u32 geometryGroup = pantsDisplayInfo->goesetGroup[0];
                    u32 geometryGroupID = 1001 + geometryGroup;

                    if (geometryGroup > 2)
                    {
                        DisableGeometryGroups(registry, entity, model, 1300, 1399);
                        EnableGeometryGroup(registry, entity, model, geometryGroupID);
                    }
                    else if (!showsTabard)
                    {
                        EnableGeometryGroup(registry, entity, model, geometryGroupID);
                    }
                }
            }
        }

        if (cloakDisplayID)
        {
            auto* cloakDisplayInfo = itemDisplayInfoStorage->TryGet<ClientDB::Definitions::ItemDisplayInfo>(cloakDisplayID);
            if (cloakDisplayInfo && cloakDisplayInfo->goesetGroup[0])
            {
                DisableGeometryGroups(registry, entity, model, 1500, 1599);

                u32 geometryGroupID = 1501 + cloakDisplayInfo->goesetGroup[0];
                EnableGeometryGroup(registry, entity, model, geometryGroupID);
            }
        }

        if (beltDisplayID)
        {
            auto* beltDisplayInfo = itemDisplayInfoStorage->TryGet<ClientDB::Definitions::ItemDisplayInfo>(beltDisplayID);
            if (beltDisplayInfo && beltDisplayInfo->goesetGroup[0])
            {
                DisableGeometryGroups(registry, entity, model, 1800, 1899);

                u32 geometryGroupID = 1801 + beltDisplayInfo->goesetGroup[0];
                EnableGeometryGroup(registry, entity, model, geometryGroupID);
            }
        }
    }

    ::Animation::Defines::Type GetIdleAnimation(bool isSwimming, bool stealthed)
    {
        auto animation = ::Animation::Defines::Type::Stand;

        if (isSwimming)
        {
            animation = ::Animation::Defines::Type::SwimIdle;
        }
        else if (stealthed)
        {
            animation = ::Animation::Defines::Type::StealthStand;
        }

        return animation;
    }

    ::Animation::Defines::Type GetMoveForwardAnimation(f32 speed, bool isSwimming, bool stealthed)
    {
        auto animation = ::Animation::Defines::Type::Walk;

        if (isSwimming)
        {
            animation = ::Animation::Defines::Type::Swim;
        }
        else if (stealthed)
        {
            animation = ::Animation::Defines::Type::StealthWalk;
        }
        else
        {
            if (speed >= 11.0f)
            {
                animation = ::Animation::Defines::Type::Sprint;
            }
            else if (speed > 4.0f)
            {
                animation = ::Animation::Defines::Type::Run;
            }
        }

        return animation;
    }

    ::Animation::Defines::Type GetMoveBackwardAnimation(bool isSwimming)
    {
        auto animation = isSwimming ? ::Animation::Defines::Type::SwimBackwards : ::Animation::Defines::Type::Walkbackwards;
        return animation;
    }

    ::Animation::Defines::Type GetMoveLeftAnimation(f32 speed, bool isSwimming, bool stealthed)
    {
        auto animation = ::Animation::Defines::Type::SwimLeft;
        if (!isSwimming)
            animation = GetMoveForwardAnimation(speed, isSwimming, stealthed);

        return animation;
    }
    ::Animation::Defines::Type GetMoveRightAnimation(f32 speed, bool isSwimming, bool stealthed)
    {
        auto animation = ::Animation::Defines::Type::SwimRight;
        if (!isSwimming)
            animation = GetMoveForwardAnimation(speed, isSwimming, stealthed);

        return animation;
    }
}
