#pragma once
#include "Game-Lib/Gameplay/Animation/Defines.h"

#include <entt/fwd.hpp>

#include <robinhood/robinhood.h>

namespace ECS::Singletons
{
    struct AnimationSingleton
    {
    public:
        robin_hood::unordered_map<::Animation::Defines::ModelID, entt::entity> staticModelIDToEntity;
    };
}