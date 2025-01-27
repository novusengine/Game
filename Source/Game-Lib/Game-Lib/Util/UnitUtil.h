#pragma once
#include "Game-Lib/Gameplay/Animation/Defines.h"
#include "Game-Lib/Gameplay/Attachment/Defines.h"

#include <Base/Types.h>

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
    bool PlayAnimationRaw(const Model::ComplexModel* modelInfo, ::ECS::Components::AnimationData& animationData, u32 boneIndex, ::Animation::Defines::Type animationID, bool propagateToChildren = false, ::Animation::Defines::Flags flags = ::Animation::Defines::Flags::None, ::Animation::Defines::BlendOverride blendOverride = ::Animation::Defines::BlendOverride::Auto, ::Animation::Defines::SequenceInterruptCallback callback = nullptr);
    bool PlayAnimation(const Model::ComplexModel* modelInfo, ::ECS::Components::AnimationData& animationData, ::Animation::Defines::Bone bone, ::Animation::Defines::Type animationID, bool propagateToChildren = false, ::Animation::Defines::Flags flags = ::Animation::Defines::Flags::None, ::Animation::Defines::BlendOverride blendOverride = ::Animation::Defines::BlendOverride::Auto, ::Animation::Defines::SequenceInterruptCallback callback = nullptr);
    bool UpdateAnimationState(entt::registry& registry, entt::entity entity, ::ECS::Components::Model& model, f32 deltaTime);

    bool IsHandClosed(entt::registry& registry, entt::entity entity, bool isLeftHand);
    bool CloseHand(entt::registry& registry, entt::entity entity, bool isLeftHand);
    bool OpenHand(entt::registry& registry, entt::entity entity, bool isLeftHand);
    bool AddItemToHand(entt::registry& registry, entt::entity entity, ::Attachment::Defines::Type attachment, u32 displayID, entt::entity& itemEntity);
}