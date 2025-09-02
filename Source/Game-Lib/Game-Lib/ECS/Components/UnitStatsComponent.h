#pragma once
#include <Base/Types.h>

#include <Meta/Generated/Shared/UnitEnum.h>

namespace ECS::Components
{
    struct UnitStatsComponent
    {
    public:
        f32 baseHealth;
        f32 currentHealth;
        f32 maxHealth;

        f32 basePower[(u32)Generated::PowerTypeEnum::Count];
        f32 currentPower[(u32)Generated::PowerTypeEnum::Count];
        f32 maxPower[(u32)Generated::PowerTypeEnum::Count];

        i32 baseStat[(u32)Generated::StatTypeEnum::Count];
        i32 currentStat[(u32)Generated::StatTypeEnum::Count];

        i32 baseResistance[(u32)Generated::ResistanceTypeEnum::Count];
        i32 currentResistance[(u32)Generated::ResistanceTypeEnum::Count];
    };
}