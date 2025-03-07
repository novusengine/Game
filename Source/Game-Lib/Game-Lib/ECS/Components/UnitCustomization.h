#pragma once
#include <Base/Types.h>

#include <Renderer/Descriptors/TextureDesc.h>

#include <Gameplay/GameDefine.h>

namespace ECS::Components
{
    struct UnitComponentSectionsInUse
    {
    public:
        u16 fullBody : 1 = 0;
        u16 headUpper : 1 = 0;
        u16 headLower : 1 = 0;
        u16 torsoUpper : 1 = 0;
        u16 torsoLower : 1 = 0;
        u16 armUpper : 1 = 0;
        u16 armLower : 1 = 0;
        u16 hand : 1 = 0;
        u16 legUpper : 1 = 0;
        u16 legLower : 1 = 0;
        u16 foot : 1 = 0;
    };

    struct UnitCustomizationFlags
    {
    public:
        u8 useCustomSkin : 1 = 0;
        u8 forceRefresh : 1 = 0;
        u8 hairChanged : 1 = 0;
        u8 hasGloveModel : 1 = 0;
        u8 hasBeltModel : 1 = 0;
        u8 hasChestDress : 1 = 0;
        u8 hasPantsDress : 1 = 0;
    };

    struct UnitCustomization
    {
    public:
        u8 skinID = 0;
        u8 faceID = 0;
        u8 facialHairID = 255;
        u8 hairStyleID = 0;
        u8 hairColorID = 0;
        u8 earringsID = 255;
        u8 piercingsID = 255;
        u8 tattoosID = 255;
        u8 featuresID = 255;
        u8 tusksID = 255;
        u8 hornStyleID = 255;
        u8 hornColorID = 255;
        
        UnitCustomizationFlags flags;
        UnitComponentSectionsInUse componentSectionsInUse;

        Renderer::TextureID skinTextureID = Renderer::TextureID::Invalid();
    };
}