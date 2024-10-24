#pragma once
#include "Game-Lib/Animation/AnimationDefines.h"
#include "Game-Lib/Animation/AnimationSystem.h"
#include "Game-Lib/ECS/Components/AnimationData.h"
#include "Game-Lib/ECS/Components/Model.h"

#include <Base/Types.h>

#include <entt/fwd.hpp>

namespace ClientDB::Definitions
{
    struct AnimationData;
}

namespace Util::Animation
{
    const ::ClientDB::Definitions::AnimationData* GetAnimationDataRec(entt::registry& registry, ::Animation::AnimationType type);
    bool SetBoneSequence(entt::registry& registry, const ECS::Components::Model& model, ECS::Components::AnimationData& animationData, ::Animation::AnimationBone bone, ::Animation::AnimationType animationType);

    ::Animation::AnimationSequenceID GetSequenceIndexForAnimation(const ::Animation::AnimationSkeleton& skeleton, ::Animation::AnimationType animationType, i8& timesToRepeat);
}