#pragma once
#include "Game-Lib/Animation/AnimationSystem.h"

#include <Base/Types.h>

#include <entt/fwd.hpp>

namespace ClientDB::Definitions
{
    struct AnimationData;
}

namespace Util::Animation
{
    const ::ClientDB::Definitions::AnimationData* GetAnimationDataRec(entt::registry& registry, ::Animation::Type type);
}