#pragma once
#include "Game-Lib/Gameplay/Database/Unit.h"

#include <Base/Types.h>

#include <Renderer/Descriptors/TextureDesc.h>

#include <robinhood/robinhood.h>

namespace ECS
{
    namespace Singletons
    {
        struct UnitCustomizationSingleton
        {
        public:
            UnitCustomizationSingleton() {}

            robin_hood::unordered_map<u32, Database::Unit::UnitModelInfo> modelIDToUnitModelInfo;
            robin_hood::unordered_map<u32, GameDefine::UnitRace> displayIDToUnitRace;

            robin_hood::unordered_map<::Database::Unit::UnitTextureSection::Type, u32> unitTextureSectionTypeToID;
            robin_hood::unordered_map<u32, std::vector<u32>> unitBaseCustomizationKeyToChoiceIDList;

            robin_hood::unordered_map<u32, u32> unitCustomizationKeyToTextureHash;
            robin_hood::unordered_map<u32, Renderer::TextureID> unitCustomizationKeyToTextureID;
            robin_hood::unordered_map<u32, Renderer::TextureID> customizationMaterialIDToTextureID;
            robin_hood::unordered_map<u32, Renderer::TextureID> itemTextureHashToTextureID;

            robin_hood::unordered_map<u32, u16> geosetIDToGeosetKey;

            robin_hood::unordered_map<u32, u32> choiceIDToOptionData;
            robin_hood::unordered_map<u32, u32> choiceIDToGeosetID;
            robin_hood::unordered_map<u32, u32> choiceIDToTextureSectionID1;
            robin_hood::unordered_map<u32, u32> choiceIDToTextureSectionID2;
            robin_hood::unordered_map<u32, u32> choiceIDToTextureSectionID3;
            robin_hood::unordered_map<u32, u32> choiceIDToTextureHash1;
            robin_hood::unordered_map<u32, u32> choiceIDToTextureHash2;
            robin_hood::unordered_map<u32, u32> choiceIDToTextureHash3;
        };
    }
}