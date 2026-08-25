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

}
