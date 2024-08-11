#pragma once
#include "Game/Animation/AnimationSystem.h"

#include <Base/Types.h>

#include <entt/fwd.hpp>

namespace Util::Unit
{
    bool PlayAnimation(Animation::InstanceID instanceID, Animation::Bone bone, Animation::Type animationID, Animation::Flag flags = Animation::Flag::Loop, Animation::BlendOverride blendOverride = Animation::BlendOverride::Auto, Animation::AnimationCallback callback = nullptr);
    bool UpdateAnimationState(entt::registry& registry, entt::entity entity, Animation::InstanceID instanceID, f32 deltaTime);
}