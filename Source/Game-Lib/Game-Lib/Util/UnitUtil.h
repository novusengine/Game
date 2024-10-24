#pragma once
#include "Game-Lib/Animation/AnimationSystem.h"

#include <Base/Types.h>

#include <entt/fwd.hpp>

namespace Util::Unit
{
    bool PlayAnimation(::Animation::AnimationInstanceID instanceID, ::Animation::AnimationBone bone, ::Animation::AnimationType animationID, ::Animation::AnimationFlags flags = ::Animation::AnimationFlags::None, ::Animation::AnimationBlendOverride blendOverride = ::Animation::AnimationBlendOverride::Auto, ::Animation::AnimationCallback callback = nullptr);
    bool UpdateAnimationState(entt::registry& registry, entt::entity entity, ::Animation::AnimationInstanceID instanceID, f32 deltaTime);
}