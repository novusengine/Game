#include "UnitUtil.h"

#include "Game-Lib/ECS/Components/AABB.h"
#include "Game-Lib/ECS/Components/AnimationData.h"
#include "Game-Lib/ECS/Components/AttachmentData.h"
#include "Game-Lib/ECS/Components/CastInfo.h"
#include "Game-Lib/ECS/Components/DisplayInfo.h"
#include "Game-Lib/ECS/Components/Model.h"
#include "Game-Lib/ECS/Components/Name.h"
#include "Game-Lib/ECS/Components/MovementInfo.h"
#include "Game-Lib/ECS/Components/UnitCustomization.h"
#include "Game-Lib/ECS/Components/UnitEquipment.h"
#include "Game-Lib/ECS/Components/UnitStatsComponent.h"
#include "Game-Lib/ECS/Singletons/Database/ClientDBSingleton.h"
#include "Game-Lib/ECS/Singletons/Database/ItemSingleton.h"
#include "Game-Lib/ECS/Util/Transforms.h"
#include "Game-Lib/ECS/Util/Database/ItemUtil.h"
#include "Game-Lib/ECS/Util/Database/UnitCustomizationUtil.h"
#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Rendering/Model/ModelLoader.h"
#include "Game-Lib/Rendering/Texture/TextureRenderer.h"
#include "Game-Lib/Util/AnimationUtil.h"
#include "Game-Lib/Util/AttachmentUtil.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Base/Util/StringUtils.h>

#include <Meta/Generated/ClientDB.h>

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

            auto blendOverride = isFlying ? ::Animation::Defines::BlendOverride::Auto : ::Animation::Defines::BlendOverride::None;
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

    bool AddHelm(entt::registry& registry, const entt::entity entity, const Generated::ItemRecord& item, GameDefine::UnitRace race, GameDefine::Gender gender, entt::entity& itemEntity)
    {
        itemEntity = entt::null;

        if (!registry.all_of<Components::AttachmentData, Components::Model>(entity))
            return false;

        entt::registry* dbRegistry = ServiceLocator::GetEnttRegistries()->dbRegistry;
        auto& clientDBSingleton = dbRegistry->ctx().get<Singletons::ClientDBSingleton>();
        auto& itemSingleton = dbRegistry->ctx().get<Singletons::ItemSingleton>();
        auto* itemDisplayInfoStorage = clientDBSingleton.Get(ClientDBHash::ItemDisplayInfo);

        if (item.displayID == 0 || !itemDisplayInfoStorage->Has(item.displayID))
            return false;

        auto& itemDisplayInfo = itemDisplayInfoStorage->Get<Generated::ItemDisplayInfoRecord>(item.displayID);
        u32 modelResourcesID = itemDisplayInfo.modelResourcesID[0];

        if (!itemSingleton.helmModelResourcesIDToModelMapping.contains(modelResourcesID))
            return false;

        auto* itemArmorTemplateStorage = clientDBSingleton.Get(ClientDBHash::ItemArmorTemplate);
        bool hasArmorTemplate = item.armorTemplateID != 0 && itemArmorTemplateStorage->Has(item.armorTemplateID);
        if (!hasArmorTemplate)
            return false;

        auto& armorTemplate = itemArmorTemplateStorage->Get<Generated::ItemArmorTemplateRecord>(item.armorTemplateID);
        auto equipType = static_cast<Database::Item::ItemArmorEquipType>(armorTemplate.equipType);
        if (equipType != Database::Item::ItemArmorEquipType::Helm)
            return false;

        u8 helmVariant = 0;
        u32 itemModelHash = ::ECSUtil::Item::GetModelHashForHelm(itemSingleton, modelResourcesID, race, gender, helmVariant);
        return AddItemToAttachment(registry, entity, ::Attachment::Defines::Type::Helm, item.displayID, itemEntity, itemModelHash, helmVariant);
    }

    bool AddShoulders(entt::registry& registry, const entt::entity entity, const Generated::ItemRecord& item, entt::entity& shoulderLeftEntity, entt::entity& shoulderRightEntity)
    {
        shoulderLeftEntity = entt::null;
        shoulderRightEntity = entt::null;

        if (!registry.all_of<Components::AttachmentData, Components::Model>(entity))
            return false;

        entt::registry* dbRegistry = ServiceLocator::GetEnttRegistries()->dbRegistry;
        auto& clientDBSingleton = dbRegistry->ctx().get<Singletons::ClientDBSingleton>();
        auto& itemSingleton = dbRegistry->ctx().get<Singletons::ItemSingleton>();
        auto* itemDisplayInfoStorage = clientDBSingleton.Get(ClientDBHash::ItemDisplayInfo);

        if (item.displayID == 0 || !itemDisplayInfoStorage->Has(item.displayID))
            return false;

        auto& itemDisplayInfo = itemDisplayInfoStorage->Get<Generated::ItemDisplayInfoRecord>(item.displayID);
        u32 modelResourcesID = itemDisplayInfo.modelResourcesID[0];

        if (!itemSingleton.shoulderModelResourcesIDToModelMapping.contains(modelResourcesID))
            return false;

        auto* itemArmorTemplateStorage = clientDBSingleton.Get(ClientDBHash::ItemArmorTemplate);
        bool hasArmorTemplate = item.armorTemplateID != 0 && itemArmorTemplateStorage->Has(item.armorTemplateID);
        if (!hasArmorTemplate)
            return false;

        auto& armorTemplate = itemArmorTemplateStorage->Get<Generated::ItemArmorTemplateRecord>(item.armorTemplateID);
        auto equipType = static_cast<Database::Item::ItemArmorEquipType>(armorTemplate.equipType);
        if (equipType != Database::Item::ItemArmorEquipType::Shoulders)
            return false;

        u32 shoulderLeftModelHash;
        u32 shoulderRightModelHash;
        ECSUtil::Item::GetModelHashesForShoulders(itemSingleton, modelResourcesID, shoulderLeftModelHash, shoulderRightModelHash);

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

    bool AddWeaponToHand(entt::registry& registry, const entt::entity entity, const Generated::ItemRecord& item, const bool isOffHand, entt::entity& itemEntity)
    {
        itemEntity = entt::null;
            
        if (!registry.all_of<Components::AttachmentData, Components::Model>(entity))
            return false;

        entt::registry* dbRegistry = ServiceLocator::GetEnttRegistries()->dbRegistry;
        auto& clientDBSingleton = dbRegistry->ctx().get<Singletons::ClientDBSingleton>();
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
            auto& itemWeaponTemplate = itemWeaponTemplateStorage->Get<Generated::ItemWeaponTemplateRecord>(item.weaponTemplateID);
            auto weaponStyle = static_cast<Database::Item::ItemWeaponStyle>(itemWeaponTemplate.weaponStyle);

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
        
        if (!modelLoader->LoadDisplayIDForEntity(attachedEntity, attachedModel, Database::Unit::DisplayInfoType::Item, displayID, modelHash, modelVariant))
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

    void EnableGeometryGroup(entt::registry& registry, entt::entity entity, const ::ECS::Components::Model& model, u32 groupID)
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

    void DisableGeometryGroups(entt::registry& registry, entt::entity entity, const ::ECS::Components::Model& model, u32 startGroupID, u32 endGroupID)
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

    void DisableAllGeometryGroups(entt::registry& registry, entt::entity entity, const ::ECS::Components::Model& model)
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

    void RefreshGeometryGroups(entt::registry& registry, entt::entity entity, ECS::Singletons::ClientDBSingleton& clientDBSingleton, ECS::Singletons::UnitCustomizationSingleton& unitCustomizationSingleton, const ::ECS::Components::Model& model)
    {
        auto* itemStorage = clientDBSingleton.Get(ClientDBHash::Item);
        auto* itemDisplayInfoStorage = clientDBSingleton.Get(ClientDBHash::ItemDisplayInfo);

        auto& displayInfo = registry.get<::ECS::Components::DisplayInfo>(entity);
        auto& unitEquipment = registry.get<::ECS::Components::UnitEquipment>(entity);
        auto& unitCustomization = registry.get<::ECS::Components::UnitCustomization>(entity);

        {
            // Hair
            {
                DisableGeometryGroups(registry, entity, model, 1, 99);

                u16 hairGeosetID;
                if (ECSUtil::UnitCustomization::GetGeosetFromOptionValue(unitCustomizationSingleton, displayInfo.race, displayInfo.gender, Database::Unit::CustomizationOption::Hairstyle, unitCustomization.hairStyleID, hairGeosetID))
                {
                    EnableGeometryGroup(registry, entity, model, hairGeosetID);
                }
                else
                {
                    EnableGeometryGroup(registry, entity, model, 1);
                }
            }

            // Piercings
            {
                u16 piercingGeosetID;
                if (ECSUtil::UnitCustomization::GetGeosetFromOptionValue(unitCustomizationSingleton, displayInfo.race, displayInfo.gender, Database::Unit::CustomizationOption::Piercings, unitCustomization.piercingsID, piercingGeosetID))
                {
                    DisableGeometryGroups(registry, entity, model, 301, 399);
                    EnableGeometryGroup(registry, entity, model, piercingGeosetID);
                }
            }
        }

        auto GetItemDisplayID = [&itemStorage, &itemDisplayInfoStorage, &unitEquipment](Database::Item::ItemEquipSlot equipSlot) -> u32
        {
            u32 itemID = unitEquipment.equipmentSlotToItemID[(u32)equipSlot];
            if (itemID == 0)
                return 0;

            const auto& itemInfo = itemStorage->Get<::Generated::ItemRecord>(itemID);
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
        ::Util::Unit::EnableGeometryGroup(registry, entity, model, 101);
        ::Util::Unit::EnableGeometryGroup(registry, entity, model, 201);
        ::Util::Unit::EnableGeometryGroup(registry, entity, model, 301);
        ::Util::Unit::EnableGeometryGroup(registry, entity, model, 401);
        ::Util::Unit::EnableGeometryGroup(registry, entity, model, 501);
        ::Util::Unit::EnableGeometryGroup(registry, entity, model, 702);
        ::Util::Unit::EnableGeometryGroup(registry, entity, model, 1101);
        ::Util::Unit::EnableGeometryGroup(registry, entity, model, 1301);
        ::Util::Unit::EnableGeometryGroup(registry, entity, model, 1501);

        unitCustomization.flags.hasGloveModel = false;

        if (glovesDisplayID > 0)
        {
            auto* glovesDisplayInfo = itemDisplayInfoStorage->TryGet<Generated::ItemDisplayInfoRecord>(glovesDisplayID);
            if (glovesDisplayInfo && glovesDisplayInfo->modelGeosetGroups[0])
            {
                DisableGeometryGroups(registry, entity, model, 401, 499);

                u32 geometryGroupID = 401 + glovesDisplayInfo->modelGeosetGroups[0];
                EnableGeometryGroup(registry, entity, model, geometryGroupID);

                unitCustomization.flags.hasGloveModel = true;
            }
        }
        
        bool chestEnabled800 = false;
        if (!unitCustomization.flags.hasGloveModel && chestDisplayID > 0)
        {
            auto* chestDisplayInfo = itemDisplayInfoStorage->TryGet<Generated::ItemDisplayInfoRecord>(chestDisplayID);
            if (chestDisplayInfo && chestDisplayInfo->modelGeosetGroups[0])
            {
                u32 geometryGroupID = 801 + chestDisplayInfo->modelGeosetGroups[0];
                EnableGeometryGroup(registry, entity, model, geometryGroupID);

                chestEnabled800 = true;
            }
        }

        auto* tabardDisplayInfo = itemDisplayInfoStorage->TryGet<Generated::ItemDisplayInfoRecord>(tabardDisplayID);
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
            auto* chestDisplayInfo = itemDisplayInfoStorage->TryGet<Generated::ItemDisplayInfoRecord>(chestDisplayID);
            if (chestDisplayInfo && chestDisplayInfo->modelGeosetGroups[3])
            {
                u32 geometryGroupID = 2201 + chestDisplayInfo->modelGeosetGroups[3];
                DisableGeometryGroups(registry, entity, model, 2200, 2299);
                EnableGeometryGroup(registry, entity, model, geometryGroupID);
            }
        }
        
        unitCustomization.flags.hasBeltModel = false;

        auto* beltDisplayInfo = itemDisplayInfoStorage->TryGet<Generated::ItemDisplayInfoRecord>(beltDisplayID);
        if (beltDisplayInfo)
            unitCustomization.flags.hasBeltModel = beltDisplayInfo->flags & 0x200;

        unitCustomization.flags.hasChestDress = false;
        unitCustomization.flags.hasPantsDress = false;

        if (chestDisplayID > 0)
        {
            auto* chestDisplayInfo = itemDisplayInfoStorage->TryGet<Generated::ItemDisplayInfoRecord>(chestDisplayID);
            if (chestDisplayInfo && chestDisplayInfo->modelGeosetGroups[2])
            {
                unitCustomization.flags.hasChestDress = true;
                unitCustomization.flags.hasPantsDress = false;

                DisableGeometryGroups(registry, entity, model, 501, 599);
                DisableGeometryGroups(registry, entity, model, 902, 999);
                DisableGeometryGroups(registry, entity, model, 1100, 1199);
                DisableGeometryGroups(registry, entity, model, 1300, 1399);

                u32 geometryGroupID = 1301 + chestDisplayInfo->modelGeosetGroups[2];
                EnableGeometryGroup(registry, entity, model, geometryGroupID);
            }
        }
        else if (pantsDisplayID > 0)
        {
            auto* pantsDisplayInfo = itemDisplayInfoStorage->TryGet<Generated::ItemDisplayInfoRecord>(pantsDisplayID);
            if (pantsDisplayInfo && pantsDisplayInfo->modelGeosetGroups[2])
            {
                unitCustomization.flags.hasChestDress = false;
                unitCustomization.flags.hasPantsDress = true;

                DisableGeometryGroups(registry, entity, model, 501, 599);
                DisableGeometryGroups(registry, entity, model, 902, 999);
                DisableGeometryGroups(registry, entity, model, 1100, 1199);
                DisableGeometryGroups(registry, entity, model, 1300, 1399);

                u32 geometryGroupID = 1301 + pantsDisplayInfo->modelGeosetGroups[2];
                EnableGeometryGroup(registry, entity, model, geometryGroupID);
            }
        }

        bool hasDress = unitCustomization.flags.hasChestDress || unitCustomization.flags.hasPantsDress;
        if (!hasDress)
        {
            auto* bootsDisplayInfo = itemDisplayInfoStorage->TryGet<Generated::ItemDisplayInfoRecord>(bootsDisplayID);
            if (bootsDisplayInfo && bootsDisplayInfo->modelGeosetGroups[0])
            {
                DisableGeometryGroups(registry, entity, model, 501, 599);
                EnableGeometryGroup(registry, entity, model, 901);

                u32 geometryGroupID = 501 + bootsDisplayInfo->modelGeosetGroups[0];
                EnableGeometryGroup(registry, entity, model, geometryGroupID);
            }
            else
            {
                auto* pantsDisplayInfo = itemDisplayInfoStorage->TryGet<Generated::ItemDisplayInfoRecord>(pantsDisplayID);
                u32 geometryGroupID = pantsDisplayInfo && pantsDisplayInfo->modelGeosetGroups[1] ? 901 + pantsDisplayInfo->modelGeosetGroups[1] : 901;
                EnableGeometryGroup(registry, entity, model, geometryGroupID);
            }

            if (!unitCustomization.flags.hasGloveModel && !chestEnabled800)
            {
                auto* shirtDisplayInfo = itemDisplayInfoStorage->TryGet<Generated::ItemDisplayInfoRecord>(shirtDisplayID);
                if (shirtDisplayInfo && shirtDisplayInfo->modelGeosetGroups[0])
                {
                    u32 geometryGroupID = 801 + shirtDisplayInfo->modelGeosetGroups[0];
                    EnableGeometryGroup(registry, entity, model, geometryGroupID);
                }
            }
        }

        // Boots
        {
            bool bootsFlag = false;
            auto* bootsDisplayInfo = itemDisplayInfoStorage->TryGet<Generated::ItemDisplayInfoRecord>(bootsDisplayID);
            if (bootsDisplayInfo)
                bootsFlag = (bootsDisplayInfo->flags & 0x100000) == 0;

            u32 bootsGeometryGroupID = 0;
            if (bootsDisplayInfo && bootsDisplayInfo->modelGeosetGroups[1])
            {
                bootsGeometryGroupID = 2000 + bootsDisplayInfo->modelGeosetGroups[1];
            }
            else
            {
                bootsGeometryGroupID = bootsFlag ? 2002 : 2001;
            }
            EnableGeometryGroup(registry, entity, model, bootsGeometryGroupID);
        }

        bool showsTabard = false;
        if (!hasDress && tabardDisplayID > 0 && tabardDisplayInfo && tabardDisplayInfo->modelGeosetGroups[0])
        {
            showsTabard = true;
            u32 geometryGroupID = unitCustomization.flags.hasBeltModel ? 1203 : 1201 + tabardDisplayInfo->modelGeosetGroups[0];
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

        if (!unitCustomization.flags.hasChestDress && !showsTabard)
        {
            if (chestDisplayID)
            {
                auto* chestDisplayInfo = itemDisplayInfoStorage->TryGet<Generated::ItemDisplayInfoRecord>(chestDisplayID);
                if (chestDisplayInfo && chestDisplayInfo->modelGeosetGroups[1])
                {
                    u32 geometryGroupID = 1001 + chestDisplayInfo->modelGeosetGroups[1];
                    EnableGeometryGroup(registry, entity, model, geometryGroupID);
                }
            }
            else if (shirtDisplayID)
            {
                auto* shirtDisplayInfo = itemDisplayInfoStorage->TryGet<Generated::ItemDisplayInfoRecord>(shirtDisplayID);
                if (shirtDisplayInfo && shirtDisplayInfo->modelGeosetGroups[1])
                {
                    u32 geometryGroupID = 1001 + shirtDisplayInfo->modelGeosetGroups[1];
                    EnableGeometryGroup(registry, entity, model, geometryGroupID);
                }
            }
        }

        if (!unitCustomization.flags.hasChestDress)
        {
            if (pantsDisplayID)
            {
                auto* pantsDisplayInfo = itemDisplayInfoStorage->TryGet<Generated::ItemDisplayInfoRecord>(pantsDisplayID);
                if (pantsDisplayInfo && pantsDisplayInfo->modelGeosetGroups[0])
                {
                    u32 geometryGroup = pantsDisplayInfo->modelGeosetGroups[0];
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
            auto* cloakDisplayInfo = itemDisplayInfoStorage->TryGet<Generated::ItemDisplayInfoRecord>(cloakDisplayID);
            if (cloakDisplayInfo && cloakDisplayInfo->modelGeosetGroups[0])
            {
                DisableGeometryGroups(registry, entity, model, 1500, 1599);

                u32 geometryGroupID = 1501 + cloakDisplayInfo->modelGeosetGroups[0];
                EnableGeometryGroup(registry, entity, model, geometryGroupID);
            }
        }

        if (beltDisplayID)
        {
            auto* beltDisplayInfo = itemDisplayInfoStorage->TryGet<Generated::ItemDisplayInfoRecord>(beltDisplayID);
            if (beltDisplayInfo && beltDisplayInfo->modelGeosetGroups[0])
            {
                DisableGeometryGroups(registry, entity, model, 1800, 1899);

                u32 geometryGroupID = 1801 + beltDisplayInfo->modelGeosetGroups[0];
                EnableGeometryGroup(registry, entity, model, geometryGroupID);
            }
        }
    }

    void RefreshSkinTexture(entt::registry& registry, entt::entity entity, ECS::Singletons::ClientDBSingleton& clientDBSingleton, ECS::Singletons::UnitCustomizationSingleton& unitCustomizationSingleton, const ::ECS::Components::DisplayInfo& displayInfo, ::ECS::Components::UnitCustomization& unitCustomization, const ::ECS::Components::Model& model)
    {
        // TODO : Get Customization Info from DisplayID if possible, otherwise use UnitCustomization
        Renderer::TextureID baseSkinTextureID;
        if (!ECSUtil::UnitCustomization::GetBaseSkinTextureID(unitCustomizationSingleton, displayInfo.race, displayInfo.gender, unitCustomization.skinID, baseSkinTextureID))
            return;

        ModelLoader* modelLoader = ServiceLocator::GetGameRenderer()->GetModelLoader();
        TextureRenderer* textureRenderer = ServiceLocator::GetGameRenderer()->GetTextureRenderer();

        bool justCreatedSkinTexture = false;
        if (unitCustomization.skinTextureID == Renderer::TextureID::Invalid())
        {
            unitCustomization.skinTextureID = textureRenderer->MakeRenderableCopy(baseSkinTextureID, 512, 512);
            justCreatedSkinTexture = true;
        }

        if (justCreatedSkinTexture || unitCustomization.flags.forceRefresh)
        {
            modelLoader->SetSkinTextureForModel(model, unitCustomization.skinTextureID);
        }

        if (displayInfo.displayID != 0 && displayInfo.race != GameDefine::UnitRace::None && displayInfo.gender != GameDefine::Gender::None)
        {
            if (!justCreatedSkinTexture)
                ECSUtil::UnitCustomization::WriteBaseSkin(clientDBSingleton, unitCustomizationSingleton, unitCustomization.skinTextureID, baseSkinTextureID);

            Renderer::TextureID skinBraTextureID;
            if (ECSUtil::UnitCustomization::GetBaseSkinBraTextureID(unitCustomizationSingleton, displayInfo.race, displayInfo.gender, unitCustomization.skinID, skinBraTextureID))
            {
                ECSUtil::UnitCustomization::WriteTextureToSkin(clientDBSingleton, unitCustomizationSingleton, unitCustomization.skinTextureID, skinBraTextureID, Database::Unit::TextureSectionType::TorsoUpper);
            }

            Renderer::TextureID skinUnderwearTextureID;
            if (ECSUtil::UnitCustomization::GetBaseSkinUnderwearTextureID(unitCustomizationSingleton, displayInfo.race, displayInfo.gender, unitCustomization.skinID, skinUnderwearTextureID))
            {
                ECSUtil::UnitCustomization::WriteTextureToSkin(clientDBSingleton, unitCustomizationSingleton, unitCustomization.skinTextureID, skinUnderwearTextureID, Database::Unit::TextureSectionType::LegUpper);
            }

            Renderer::TextureID skinFaceTextureID1;
            if (ECSUtil::UnitCustomization::GetBaseSkinFaceTextureID(unitCustomizationSingleton, displayInfo.race, displayInfo.gender, unitCustomization.skinID, unitCustomization.faceID, 0, skinFaceTextureID1))
            {
                ECSUtil::UnitCustomization::WriteTextureToSkin(clientDBSingleton, unitCustomizationSingleton, unitCustomization.skinTextureID, skinFaceTextureID1, Database::Unit::TextureSectionType::HeadLower);
            }

            Renderer::TextureID skinFaceTextureID2;
            if (ECSUtil::UnitCustomization::GetBaseSkinFaceTextureID(unitCustomizationSingleton, displayInfo.race, displayInfo.gender, unitCustomization.skinID, unitCustomization.faceID, 1, skinFaceTextureID2))
            {
                ECSUtil::UnitCustomization::WriteTextureToSkin(clientDBSingleton, unitCustomizationSingleton, unitCustomization.skinTextureID, skinFaceTextureID2, Database::Unit::TextureSectionType::HeadUpper);
            }

            Renderer::TextureID skinHairTextureID2;
            if (ECSUtil::UnitCustomization::GetBaseSkinHairTextureID(unitCustomizationSingleton, displayInfo.race, displayInfo.gender, unitCustomization.hairStyleID, unitCustomization.hairColorID, 2, skinHairTextureID2))
            {
                ECSUtil::UnitCustomization::WriteTextureToSkin(clientDBSingleton, unitCustomizationSingleton, unitCustomization.skinTextureID, skinHairTextureID2, Database::Unit::TextureSectionType::HeadUpper);
            }

            Renderer::TextureID skinHairTextureID1;
            if (ECSUtil::UnitCustomization::GetBaseSkinHairTextureID(unitCustomizationSingleton, displayInfo.race, displayInfo.gender, unitCustomization.hairStyleID, unitCustomization.hairColorID, 1, skinHairTextureID1))
            {
                ECSUtil::UnitCustomization::WriteTextureToSkin(clientDBSingleton, unitCustomizationSingleton, unitCustomization.skinTextureID, skinHairTextureID1, Database::Unit::TextureSectionType::HeadLower);
            }

            if (unitCustomization.flags.hairChanged || unitCustomization.flags.forceRefresh)
            {
                Renderer::TextureID hairTextureID;
                if (ECSUtil::UnitCustomization::GetHairTexture(unitCustomizationSingleton, displayInfo.race, displayInfo.gender, unitCustomization.hairStyleID, unitCustomization.hairColorID, hairTextureID))
                {
                    modelLoader->SetHairTextureForModel(model, hairTextureID);
                }
                else
                {
                    modelLoader->SetHairTextureForModel(model, Renderer::TextureID::Invalid());
                }

                unitCustomization.flags.hairChanged = false;
            }
        }

        unitCustomization.flags.forceRefresh = false;
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
