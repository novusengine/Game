#pragma once
#include <Base/Types.h>

#include <Meta/Generated/Shared/UnitEnum.h>

#include <robinhood/robinhood.h>

namespace ECS
{
    struct UnitResistance
    {
    public:
        f64 base = 0.0;
        f64 current = 0.0;
        f64 max = 0.0;
    };

    namespace Components
    {
        struct UnitResistancesComponent
        {
        public:
            robin_hood::unordered_map<Generated::ResistanceTypeEnum, UnitResistance> resistanceTypeToValue;
            robin_hood::unordered_set<Generated::ResistanceTypeEnum> dirtyResistanceTypes;
        };
    }
}