#pragma once
#include <Base/Types.h>

#include <FileFormat/Novus/ClientDB/Definitions.h>

#include <Gameplay/GameDefine.h>

namespace Database::Unit
{
    struct UnitRace
    {
    public:
        ClientDB::StringRef prefix;
        ClientDB::StringRef nameInternal;
        ClientDB::StringRef name;
        u32 flags;
        u32 factionID;
        u32 maleDisplayID;
        u32 femaleDisplayID;
    };

    struct UnitModelInfo
    {
    public:
        GameDefine::UnitRace race;
        GameDefine::Gender gender;
    };

    struct UnitTextureSection
    {
    public:
        enum class Type : u8
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

        Type type;
        u16 posX;
        u16 posY;
        u16 width;
        u16 height;
    };

    enum class CustomizationOption : u32
    {
        Unused = 0,
        Start = 1,
        Skin = 1,
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

    struct UnitCustomizationOption
    {
    public:
        struct Flags
        {
            u32 isBaseSkin : 1 = 0;
            u32 isBaseSkinOption : 1 = 0;
            u32 isHairTextureColorOption : 1 = 0;
            u32 isGeosetOption : 1 = 0;
        };

        ClientDB::StringRef name;
        Flags flags = { 0 };
    };

    struct UnitCustomizationGeoset
    {
    public:
        u8 geosetType;
        u8 geosetValue;
    };

    struct UnitCustomizationMaterial
    {
    public:
        u32 textureSectionID;
        u32 materialResourcesID;
    };

    struct UnitRaceCustomizationChoice
    {
    public:
        u8 raceID;
        u8 gender;
        u32 customizationOptionID;
        u16 customizationOptionData1;
        u16 customizationOptionData2;
        u32 customizationGeosetID;
        u32 customizationMaterialID1;
        u32 customizationMaterialID2;
        u32 customizationMaterialID3;
    };
}