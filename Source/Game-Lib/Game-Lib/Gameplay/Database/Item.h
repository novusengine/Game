#pragma once
#include "Shared.h"

#include <Base/Types.h>

namespace Database::Item
{
    struct Item
    {
    public:
        u32 displayID;
        u8 bind;
        u8 rarity;
        u8 category;
        u8 type;
        u16 virtualLevel;
        u16 requiredLevel;
        u32 durability;
        u32 iconID;
        u32 name;
        u32 description;
        u32 armor;
        u32 statTemplateID;
        u32 armorTemplateID;
        u32 weaponTemplateID;
        u32 shieldTemplateID;
    };

    struct ItemStatTemplate
    {
    public:
        u8 statTypes[8];
        i32 statValues[8];
    };

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

    enum class ItemArmorEquipType : u32
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

    struct ItemArmorTemplate
    {
    public:
        ItemArmorEquipType equipType;
        u32 bonusArmor;
    };

    enum class ItemWeaponStyle : u32
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

    struct ItemWeaponTemplate
    {
    public:
        ItemWeaponStyle weaponStyle;
        u32 minDamage;
        u32 maxDamage;
        f32 speed;
    };

    struct ItemShieldTemplate
    {
    public:
        u32 bonusArmor;
        u32 block;
    };

    enum class ItemEffectType : u8
    {
        OnEquip,
        OnUse,
        OnProc,
        OnLooted,
        OnBound
    };
    struct ItemEffect
    {
    public:
        u32 itemID;
        u8 slot;
        ItemEffectType type;
        u32 spellID;
    };
}