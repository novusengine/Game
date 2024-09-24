#pragma once
#include "Game-Lib/Animation/AnimationDefines.h"

#include <entt/fwd.hpp>

#include <robinhood/robinhood.h>

namespace ECS::Singletons
{
    struct AnimationSingleton
    {
    public:
        robin_hood::unordered_map<Animation::AnimationModelID, entt::entity> staticModelIDToEntity;
    };
}