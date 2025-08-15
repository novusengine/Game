#pragma once
#include <Base/Types.h>

#include <Gameplay/GameDefine.h>

namespace Database::Unit
{
    enum class DisplayInfoType
    {
        Creature = 0,
        GameObject = 1,
        Item = 2
    };

    struct UnitModelInfo
    {
    public:
        GameDefine::UnitRace race;
        GameDefine::UnitGender gender;
    };

    enum class TextureSectionType : u8
    {
        FullBody = 0,
        HeadUpper = 1,
        HeadLower = 2,
        TorsoUpper = 3,
        TorsoLower = 4,
        ArmUpper = 5,
        ArmLower = 6,
        Hand = 7,
        LegUpper = 8,
        LegLower = 9,
        Foot = 10
    };

    enum class CustomizationOption : u32
    {
        Unused = 0,
        Skin = 1,
        Start = 1,
        SkinBra = 2,
        SkinUnderwear = 3,
        Face = 4,
        FacialHair = 5,
        Hairstyle = 6,
        HairColor = 7,
        Earrings = 8,
        Piercings = 9,
        Tattoos = 10,
        Features = 11,
        Tusks = 12,
        HornStyle = 13,
        HornColor = 14,
        Count = 15
    };

    struct UnitCustomizationOptionFlags
    {
    public:
        u32 isBaseSkin : 1 = 0;
        u32 isBaseSkinOption : 1 = 0;
        u32 isHairTextureColorOption : 1 = 0;
        u32 isGeosetOption : 1 = 0;
    };

    struct AnimationDataFlags
    {
        u64 UsedForEmote : 1 = 0;
        u64 UsedForSpell : 1 = 0;
        u64 IsPierceAnim : 1 = 0;
        u64 HideWeapons : 1 = 0;
        u64 FallbackPlaysReverse : 1 = 0;
        u64 FallbackHoldsEnd : 1 = 0;
        u64 Unused0x40 : 1 = 0;
        u64 FallbackToVariationZero : 1 = 0;
        u64 Unused0x100 : 1 = 0;
        u64 Unused0x200 : 1 = 0;
        u64 Unused0x400 : 1 = 0;
        u64 MoveWeaponsToSheath : 1 = 0;
        u64 MoveMeleeWeaponsToHand : 1 = 0;
        u64 ScaleToGround : 1 = 0;
        u64 ScaleToGroundRev : 1 = 0;
        u64 ScaleToGroundAlways : 1 = 0;
        u64 IsSplitBodyBehavior : 1 = 0;
        u64 IsBowWeaponBehavior : 1 = 0;
        u64 IsRifleWeaponBehavior : 1 = 0;
        u64 IsThrownWeaponBehavior : 1 = 0;
        u64 IsDeathBehavior : 1 = 0;
        u64 IsMeleeCombatBehavior : 1 = 0;
        u64 IsSpecialCombatBehavior : 1 = 0;
        u64 IsWoundBehavior : 1 = 0;
        u64 IsUnarmedBehavior : 1 = 0;
        u64 UseMountedNameplatePos : 1 = 0;
        u64 FlipSpearWeapons180Deg : 1 = 0;
        u64 Unused0x8000000 : 1 = 0;
        u64 Unused0x10000000 : 1 = 0;
        u64 IsSpellCombatBehavior : 1 = 0;
        u64 BrewmasterSheathe : 1 = 0;
        u64 Unused0x80000000 : 1 = 0;
    };
}