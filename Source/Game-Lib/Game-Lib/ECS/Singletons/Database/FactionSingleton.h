#pragma once

#include "Game-Lib/Gameplay/Faction/FactionRuntimeData.h"

#include <memory>

namespace ECS::Singletons
{
    struct FactionSingleton
    {
    public:
        std::shared_ptr<const Gameplay::Faction::FactionRuntimeData> runtime;
    };
} // namespace ECS::Singletons
