#pragma once
#include "Game-Lib/Gameplay/Animation/Defines.h"
#include "Game-Lib/Gameplay/Attachment/Defines.h"
#include "Game-Lib/Gameplay/Database/Item.h"

#include <Base/Types.h>

#include <Gameplay/GameDefine.h>

#include <entt/fwd.hpp>

namespace ECS::Components
{
    struct AnimationData;
    struct Model;
}

namespace Model
{
    struct ComplexModel;
}

namespace Util::Unit
{
    bool PlayAnimationRaw(const Model::ComplexModel* modelInfo, ::ECS::Components::AnimationData& animationData, u32 boneIndex, ::Animation::Defines::Type animationID, bool propagateToChildren = false, ::Animation::Defines::Flags flags = ::Animation::Defines::Flags::None, ::Animation::Defines::BlendOverride blendOverride = ::Animation::Defines::BlendOverride::Auto, f32 speedModifier = 1.0f, ::Animation::Defines::SequenceInterruptCallback callback = nullptr);
    bool PlayAnimation(const Model::ComplexModel* modelInfo, ::ECS::Components::AnimationData& animationData, ::Animation::Defines::Bone bone, ::Animation::Defines::Type animationID, bool propagateToChildren = false, ::Animation::Defines::Flags flags = ::Animation::Defines::Flags::None, ::Animation::Defines::BlendOverride blendOverride = ::Animation::Defines::BlendOverride::Auto, f32 speedModifier = 1.0f, ::Animation::Defines::SequenceInterruptCallback callback = nullptr);
    bool UpdateAnimationState(entt::registry& registry, entt::entity entity, ::ECS::Components::Model& model, f32 deltaTime);

    bool IsHandClosed(entt::registry& registry, entt::entity entity, bool isOffHand);
    bool CloseHand(entt::registry& registry, entt::entity entity, bool isOffHand);
    bool OpenHand(entt::registry& registry, entt::entity entity, bool isOffHand);

    bool AddHelm(entt::registry& registry, const entt::entity entity, const Database::Item::Item& item, GameDefine::UnitRace race, GameDefine::Gender gender, entt::entity& itemEntity);
    bool AddShoulders(entt::registry& registry, const entt::entity entity, const Database::Item::Item& item, entt::entity& shoulderLeftEntity, entt::entity& shoulderRightEntity);
    bool AddWeaponToHand(entt::registry& registry, const entt::entity entity, const Database::Item::Item& item, const bool isOffHand, entt::entity& itemEntity);

    bool AddItemToAttachment(entt::registry& registry, entt::entity entity, ::Attachment::Defines::Type attachment, u32 displayID, entt::entity& itemEntity, u32 modelHash = std::numeric_limits<u32>().max(), u8 modelVariant = 0);
    bool RemoveItemFromAttachment(entt::registry& registry, entt::entity entity, ::Attachment::Defines::Type attachment);

    void EnableGeometryGroup(entt::registry& registry, entt::entity entity, ::ECS::Components::Model& model, u32 groupID);
    void DisableGeometryGroups(entt::registry& registry, entt::entity entity, ::ECS::Components::Model& model, u32 startGroupID, u32 endGroupID = 0);
    void DisableAllGeometryGroups(entt::registry& registry, entt::entity entity, ::ECS::Components::Model& model);
    void RefreshGeometryGroups(entt::registry& registry, entt::entity entity, ::ECS::Components::Model& model);

    ::Animation::Defines::Type GetIdleAnimation(bool isSwimming, bool stealthed);
    ::Animation::Defines::Type GetMoveForwardAnimation(f32 speed, bool isSwimming, bool stealthed);
    ::Animation::Defines::Type GetMoveBackwardAnimation(bool isSwimming);
    ::Animation::Defines::Type GetMoveLeftAnimation(f32 speed, bool isSwimming, bool stealthed);
    ::Animation::Defines::Type GetMoveRightAnimation(f32 speed, bool isSwimming, bool stealthed);
}