#pragma once
#include <Base/Types.h>

namespace Database::Item
{
    enum class ItemEquipSlot : u8
    {
        Helm,
        Necklace,
        Shoulders,
        Cloak,
        Chest,
        Shirt,
        Tabard,
        Bracers,
        Gloves,
        Belt,
        Pants,
        Boots,
        Ring1,
        Ring2,
        Trinket1,
        Trinket2,
        MainHand,
        OffHand,
        Ranged,
        Count
    };

    enum class ItemArmorEquipType : u8
    {
        Helm        = 1,
        Necklace    = 2,
        Shoulders   = 3,
        Cloak       = 4,
        Chest       = 5,
        Shirt       = 6,
        Tabard      = 7,
        Bracers     = 8,
        Gloves      = 9,
        Belt        = 10,
        Pants       = 11,
        Boots       = 12,
        Ring        = 13,
        Trinket     = 14,
        Weapon      = 15,
        OffHand     = 16,
        Ranged      = 17,
        Ammo        = 18
    };

    enum class ItemWeaponStyle : u8
    {
        Unspecified = 1,
        OneHand     = 2,
        TwoHand     = 3,
        MainHand    = 4,
        OffHand     = 5,
        Ranged      = 6,
        Wand        = 7,
        Tool        = 8
    };

    enum class ItemEffectType : u8
    {
        OnEquip,
        OnUse,
        OnProc,
        OnLooted,
        OnBound
    };
}