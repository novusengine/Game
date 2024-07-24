#pragma once
#include <Base/Types.h>

namespace ECS::Components
{
    enum class PowerType
    {
        Health = -2, // This is strictly used for spells
        Mana = 0,
        Rage,
        Focus,
        Energy,
        Happiness,
        Count
    };

    enum class StatType
    {
        Strength,
        Agility,
        Stamina,
        Intellect,
        Spirit,
        Count
    };

    enum class ResistanceType
    {
        Normal,
        Holy,
        Fire,
        Nature,
        Frost,
        Shadow,
        Arcane,
        Count
    };

    struct UnitStatsComponent
    {
        f32 baseHealth;
        f32 currentHealth;
        f32 maxHealth;

        f32 basePower[(u32)PowerType::Count];
        f32 currentPower[(u32)PowerType::Count];
        f32 maxPower[(u32)PowerType::Count];

        i32 baseStat[(u32)StatType::Count];
        i32 currentStat[(u32)StatType::Count];

        i32 baseResistance[(u32)ResistanceType::Count];
        i32 currentResistance[(u32)ResistanceType::Count];
    };
}