#pragma once
#include "Game-Lib/Gameplay/Database/Unit.h"

#include <Base/Types.h>

#include <Gameplay/GameDefine.h>

#include <Renderer/Descriptors/TextureDesc.h>

#include <entt/fwd.hpp>

class TextureRenderer;

namespace ECS
{
    namespace Components
    {
        struct UnitCustomization;
    }

    namespace Singletons
    {
        struct ClientDBSingleton;
        struct ItemSingleton;
        struct TextureSingleton;
        struct UnitCustomizationSingleton;
    }
}

namespace ECSUtil::UnitCustomization
{
    bool Refresh();

    bool GetBaseSkinTextureID(ECS::Singletons::UnitCustomizationSingleton& unitCustomizationSingleton, GameDefine::UnitRace race, GameDefine::Gender gender, u8 skinID, Renderer::TextureID& textureID);
    bool GetBaseSkinBraTextureID(ECS::Singletons::UnitCustomizationSingleton& unitCustomizationSingleton, GameDefine::UnitRace race, GameDefine::Gender gender, u8 skinID, Renderer::TextureID& textureID);
    bool GetBaseSkinUnderwearTextureID(ECS::Singletons::UnitCustomizationSingleton& unitCustomizationSingleton, GameDefine::UnitRace race, GameDefine::Gender gender, u8 skinID, Renderer::TextureID& textureID);
    bool GetBaseSkinFaceTextureID(ECS::Singletons::UnitCustomizationSingleton& unitCustomizationSingleton, GameDefine::UnitRace race, GameDefine::Gender gender, u8 skinID, u8 faceID, u8 variant, Renderer::TextureID& textureID);
    bool GetBaseSkinHairTextureID(ECS::Singletons::UnitCustomizationSingleton& unitCustomizationSingleton, GameDefine::UnitRace race, GameDefine::Gender gender, u8 hairStyle, u8 hairColor, u8 variant, Renderer::TextureID& textureID);

    void WriteBaseSkin(ECS::Singletons::ClientDBSingleton& clientDBSingleton, ECS::Singletons::UnitCustomizationSingleton& unitCustomizationSingleton, Renderer::TextureID skinTextureID, Renderer::TextureID baseSkinTextureID, vec2 srcMin = vec2(0.0f), vec2 srcMax = vec2(1.0f));
    void WriteTextureToSkin(ECS::Singletons::ClientDBSingleton& clientDBSingleton, ECS::Singletons::UnitCustomizationSingleton& unitCustomizationSingleton, Renderer::TextureID skinTextureID, Renderer::TextureID textureID, Database::Unit::UnitTextureSection::Type textureSectionType, vec2 srcMin = vec2(0.0f), vec2 srcMax = vec2(1.0f));
    void WriteItemToSkin(ECS::Singletons::TextureSingleton& textureSingleton, ECS::Singletons::ClientDBSingleton& clientDBSingleton, ECS::Singletons::ItemSingleton& itemSingleton, ECS::Singletons::UnitCustomizationSingleton& unitCustomizationSingleton, ECS::Components::UnitCustomization& unitCustomization, u32 itemDisplayID);

    bool GetChoiceIDFromOptionValue(ECS::Singletons::UnitCustomizationSingleton& unitCustomizationSingleton, GameDefine::UnitRace race, GameDefine::Gender gender, ::Database::Unit::CustomizationOption customizationOption, u8 customizationOptionValue, u32& choiceID);
    bool GetGeosetFromOptionValue(ECS::Singletons::UnitCustomizationSingleton& unitCustomizationSingleton, GameDefine::UnitRace race, GameDefine::Gender gender, ::Database::Unit::CustomizationOption customizationOption, u8 customizationOptionValue, u16& geoset);
    bool GetHairTexture(ECS::Singletons::UnitCustomizationSingleton& unitCustomizationSingleton, GameDefine::UnitRace race, GameDefine::Gender gender, u8 hairStyle, u8 hairColor, Renderer::TextureID& textureID);

    u32 CreateCustomizationKey(GameDefine::UnitRace race, GameDefine::Gender gender, ::Database::Unit::CustomizationOption customizationOption, u8 choiceIndex = 255, u8 variationIndex = 255, u8 materialIndex = 255);
}