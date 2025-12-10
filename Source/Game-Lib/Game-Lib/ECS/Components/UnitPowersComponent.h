#pragma once
#include <Base/Types.h>

#include <MetaGen/Shared/Unit/Unit.h>

#include <robinhood/robinhood.h>

namespace ECS
{
    struct UnitPower
    {
    public:
        f64 base = 0.0;
        f64 current = 0.0;
        f64 max = 0.0;
    };

    namespace Components
    {
        struct UnitPowersComponent
        {
        public:
            robin_hood::unordered_map<MetaGen::Shared::Unit::PowerTypeEnum, UnitPower> powerTypeToValue;
            robin_hood::unordered_set<MetaGen::Shared::Unit::PowerTypeEnum> dirtyPowerTypes;
        };
    }
}