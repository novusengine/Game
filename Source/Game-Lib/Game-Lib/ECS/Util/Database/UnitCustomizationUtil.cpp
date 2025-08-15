#include "UnitCustomizationUtil.h"
#include "ItemUtil.h"
#include "TextureUtil.h"

#include "Game-Lib/ECS/Components/UnitCustomization.h"
#include "Game-Lib/ECS/Singletons/Database/ClientDBSingleton.h"
#include "Game-Lib/ECS/Singletons/Database/ItemSingleton.h"
#include "Game-Lib/ECS/Singletons/Database/TextureSingleton.h"
#include "Game-Lib/ECS/Singletons/Database/UnitCustomizationSingleton.h"
#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Rendering/Texture/TextureRenderer.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Meta/Generated/ClientDB.h>

#include <entt/entt.hpp>

#include <filesystem>

namespace ECSUtil::UnitCustomization
{
    struct ParsedTextureName
    {
        u8 typeStartValue;
        u16 typeStrOffset;

        u8 variationStartValue;
        u8 variationDigitLength;
        u16 variationStrOffset;
    };

    bool ParseFileName(const std::string& texturePath, ParsedTextureName& result)
    {
        size_t underscorePos = texturePath.rfind('_');
        if (underscorePos == std::string::npos || underscorePos < 2)
            return false;

        u8 left = static_cast<u8>(std::stoi(texturePath.substr(underscorePos - 2, 2)));
        u8 right = static_cast<u8>(std::stoi(texturePath.substr(underscorePos + 1)));

        u8 rightCount = 2 + (1 * right > 99);

        result.typeStartValue = left;
        result.typeStrOffset = static_cast<u16>(underscorePos - 2);
        result.variationStartValue = right;
        result.variationDigitLength = rightCount;
        result.variationStrOffset = static_cast<u16>(underscorePos + 1);
        return true;
    }

    bool Refresh()
    {
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->dbRegistry;
        auto& ctx = registry->ctx();
        auto& clientDBSingleton = ctx.get<ECS::Singletons::ClientDBSingleton>();
        auto& textureSingleton = ctx.get<ECS::Singletons::TextureSingleton>();

        if (!ctx.contains<ECS::Singletons::UnitCustomizationSingleton>())
            ctx.emplace<ECS::Singletons::UnitCustomizationSingleton>();

        auto& unitCustomizationSingleton = ctx.get<ECS::Singletons::UnitCustomizationSingleton>();

        if (!clientDBSingleton.Has(ClientDBHash::UnitRace))
        {
            clientDBSingleton.Register(ClientDBHash::UnitRace, "UnitRace");

            auto* storage = clientDBSingleton.Get(ClientDBHash::UnitRace);
            storage->Initialize<Generated::UnitRaceRecord>();

            storage->MarkDirty();
        }

        if (!clientDBSingleton.Has(ClientDBHash::UnitTextureSection))
        {
            clientDBSingleton.Register(ClientDBHash::UnitTextureSection, "UnitTextureSection");

            auto* storage = clientDBSingleton.Get(ClientDBHash::UnitTextureSection);
            storage->Initialize<Generated::UnitTextureSectionRecord>();

            storage->MarkDirty();
        }

        if (!clientDBSingleton.Has(ClientDBHash::UnitCustomizationOption))
        {
            clientDBSingleton.Register(ClientDBHash::UnitCustomizationOption, "UnitCustomizationOption");

            auto* storage = clientDBSingleton.Get(ClientDBHash::UnitCustomizationOption);
            storage->Initialize<Generated::UnitCustomizationOptionRecord>();

            storage->MarkDirty();
        }

        if (!clientDBSingleton.Has(ClientDBHash::UnitCustomizationGeoset))
        {
            clientDBSingleton.Register(ClientDBHash::UnitCustomizationGeoset, "UnitCustomizationGeoset");

            auto* storage = clientDBSingleton.Get(ClientDBHash::UnitCustomizationGeoset);
            storage->Initialize<Generated::UnitCustomizationGeosetRecord>();

            storage->MarkDirty();
        }

        if (!clientDBSingleton.Has(ClientDBHash::UnitCustomizationMaterial))
        {
            clientDBSingleton.Register(ClientDBHash::UnitCustomizationMaterial, "UnitCustomizationMaterial");

            auto* storage = clientDBSingleton.Get(ClientDBHash::UnitCustomizationMaterial);
            storage->Initialize<Generated::UnitCustomizationMaterialRecord>();

            storage->MarkDirty();
        }

        if (!clientDBSingleton.Has(ClientDBHash::UnitRaceCustomizationChoice))
        {
            clientDBSingleton.Register(ClientDBHash::UnitRaceCustomizationChoice, "UnitRaceCustomizationChoice");

            auto* storage = clientDBSingleton.Get(ClientDBHash::UnitRaceCustomizationChoice);
            storage->Initialize<Generated::UnitRaceCustomizationChoiceRecord>();

            storage->MarkDirty();
        }

        auto* creatureModelDataStorage = clientDBSingleton.Get(ClientDBHash::CreatureModelData);
        auto* creatureDisplayInfoStorage = clientDBSingleton.Get(ClientDBHash::CreatureDisplayInfo);
        auto* unitRaceStorage = clientDBSingleton.Get(ClientDBHash::UnitRace);
        auto* unitTextureSectionStorage = clientDBSingleton.Get(ClientDBHash::UnitTextureSection);
        auto* unitCustomizationOptionStorage = clientDBSingleton.Get(ClientDBHash::UnitCustomizationOption);
        auto* unitCustomizationGeosetStorage = clientDBSingleton.Get(ClientDBHash::UnitCustomizationGeoset);
        auto* unitCustomizationMaterialStorage = clientDBSingleton.Get(ClientDBHash::UnitCustomizationMaterial);
        auto* unitRaceCustomizationChoiceStorage = clientDBSingleton.Get(ClientDBHash::UnitRaceCustomizationChoice);

        unitCustomizationSingleton.itemTextureHashToTextureID.reserve(1024);

        u32 numUnitRaceRows = unitRaceStorage->GetNumRows();
        unitCustomizationSingleton.modelIDToUnitModelInfo.clear();
        unitCustomizationSingleton.modelIDToUnitModelInfo.reserve(numUnitRaceRows);
        unitRaceStorage->Each([&](u32 id, Generated::UnitRaceRecord& row)
        {
            u32 maleModelID = 0;
            u32 femaleModelID = 0;

            if (auto* creatureDisplayInfo = creatureDisplayInfoStorage->TryGet<Generated::CreatureDisplayInfoRecord>(row.maleDisplayID))
            {
                if (creatureModelDataStorage->Has(creatureDisplayInfo->modelID))
                    maleModelID = creatureDisplayInfo->modelID;
            }

            if (auto* creatureDisplayInfo = creatureDisplayInfoStorage->TryGet<Generated::CreatureDisplayInfoRecord>(row.femaleDisplayID))
            {
                if (creatureModelDataStorage->Has(creatureDisplayInfo->modelID))
                    femaleModelID = creatureDisplayInfo->modelID;
            }

            if (maleModelID != 0 || femaleModelID != 0)
            {
                if (maleModelID == femaleModelID)
                {
                    Database::Unit::UnitModelInfo unitModelInfo =
                    {
                        .race = static_cast<GameDefine::UnitRace>(id),
                        .gender = GameDefine::UnitGender::Other
                    };

                    unitCustomizationSingleton.modelIDToUnitModelInfo[maleModelID] = unitModelInfo;
                }
                else
                {
                    if (maleModelID != 0)
                    {
                        Database::Unit::UnitModelInfo unitModelInfo =
                        {
                            .race = static_cast<GameDefine::UnitRace>(id),
                            .gender = GameDefine::UnitGender::Male
                        };

                        unitCustomizationSingleton.modelIDToUnitModelInfo[maleModelID] = unitModelInfo;
                    }

                    if (femaleModelID != 0)
                    {
                        Database::Unit::UnitModelInfo unitModelInfo =
                        {
                            .race = static_cast<GameDefine::UnitRace>(id),
                            .gender = GameDefine::UnitGender::Female
                        };

                        unitCustomizationSingleton.modelIDToUnitModelInfo[femaleModelID] = unitModelInfo;
                    }
                }
            }

            return true;
        });

        u32 numUnitTextureSectionRows = unitTextureSectionStorage->GetNumRows();
        unitCustomizationSingleton.unitTextureSectionTypeToID.clear();
        unitCustomizationSingleton.unitTextureSectionTypeToID.reserve(numUnitTextureSectionRows);
        unitTextureSectionStorage->Each([&](u32 id, const Generated::UnitTextureSectionRecord& unitTextureSection)
        {
            unitCustomizationSingleton.unitTextureSectionTypeToID[static_cast<Database::Unit::TextureSectionType>(unitTextureSection.section)] = id;
            return true;
        });

        u32 numUnitCustomizationMaterialRows = unitCustomizationMaterialStorage->GetNumRows();
        unitCustomizationSingleton.customizationMaterialIDToTextureID.clear();
        unitCustomizationSingleton.customizationMaterialIDToTextureID.reserve(numUnitCustomizationMaterialRows);
        unitCustomizationMaterialStorage->Each([&](u32 id, const Generated::UnitCustomizationMaterialRecord& unitCustomizationMaterial)
        {
            if (id == 0)
                return true;

            if (!textureSingleton.materialResourcesIDToTextureHashes.contains(unitCustomizationMaterial.materialResourcesID))
                return true;

            u32 textureHash = textureSingleton.materialResourcesIDToTextureHashes[unitCustomizationMaterial.materialResourcesID][0];

            Renderer::TextureDesc textureDesc =
            {
                .path = ECSUtil::Texture::GetTexturePath(textureSingleton, textureHash)
            };

            Renderer::TextureID texture = ServiceLocator::GetGameRenderer()->GetRenderer()->LoadTexture(textureDesc);
            unitCustomizationSingleton.customizationMaterialIDToTextureID[id] = texture;
            return true;
        });

        u32 numCustomizationGeosetRows = unitCustomizationGeosetStorage->GetNumRows();
        unitCustomizationSingleton.geosetIDToGeosetKey.clear();
        unitCustomizationSingleton.geosetIDToGeosetKey.reserve(numCustomizationGeosetRows);
        unitCustomizationGeosetStorage->Each([&](u32 id, const Generated::UnitCustomizationGeosetRecord& unitCustomizationGeoset)
        {
            u16 key = (unitCustomizationGeoset.geosetType * 100) + unitCustomizationGeoset.geosetValue;
            unitCustomizationSingleton.geosetIDToGeosetKey[id] = key;
            return true;
        });

        u32 numUnitRaceCustomizationChoiceRows = unitRaceCustomizationChoiceStorage->GetNumRows();
        unitCustomizationSingleton.unitBaseCustomizationKeyToChoiceIDList.clear();
        unitCustomizationSingleton.unitBaseCustomizationKeyToChoiceIDList.reserve(numUnitRaceCustomizationChoiceRows);

        unitCustomizationSingleton.choiceIDToOptionData.clear();
        unitCustomizationSingleton.choiceIDToOptionData.reserve(numUnitRaceCustomizationChoiceRows);

        unitCustomizationSingleton.choiceIDToGeosetID.clear();
        unitCustomizationSingleton.choiceIDToGeosetID.reserve(numUnitRaceCustomizationChoiceRows);

        unitCustomizationSingleton.choiceIDToTextureSectionID1.clear();
        unitCustomizationSingleton.choiceIDToTextureSectionID1.reserve(numUnitRaceCustomizationChoiceRows);
        unitCustomizationSingleton.choiceIDToTextureHash1.clear();
        unitCustomizationSingleton.choiceIDToTextureHash1.reserve(numUnitRaceCustomizationChoiceRows);

        unitCustomizationSingleton.choiceIDToTextureSectionID2.clear();
        unitCustomizationSingleton.choiceIDToTextureSectionID2.reserve(numUnitRaceCustomizationChoiceRows);
        unitCustomizationSingleton.choiceIDToTextureHash2.clear();
        unitCustomizationSingleton.choiceIDToTextureHash2.reserve(numUnitRaceCustomizationChoiceRows);

        unitCustomizationSingleton.choiceIDToTextureSectionID3.clear();
        unitCustomizationSingleton.choiceIDToTextureSectionID3.reserve(numUnitRaceCustomizationChoiceRows);
        unitCustomizationSingleton.choiceIDToTextureHash3.clear();
        unitCustomizationSingleton.choiceIDToTextureHash3.reserve(numUnitRaceCustomizationChoiceRows);
        unitRaceCustomizationChoiceStorage->Each([&](u32 id, const Generated::UnitRaceCustomizationChoiceRecord& unitRaceCustomizationChoice)
        {
            if (id == 0) return true;

            auto unitRace = static_cast<GameDefine::UnitRace>(unitRaceCustomizationChoice.raceID);
            auto gender = static_cast<GameDefine::UnitGender>(unitRaceCustomizationChoice.gender + 1);
            auto customizationOption = static_cast<Database::Unit::CustomizationOption>(unitRaceCustomizationChoice.customizationOptionID);

            u32 baseCustomizationOptionKey = CreateCustomizationKey(unitRace, gender, customizationOption);
            unitCustomizationSingleton.unitBaseCustomizationKeyToChoiceIDList[baseCustomizationOptionKey].push_back(id);

            unitCustomizationSingleton.choiceIDToOptionData[id] = unitRaceCustomizationChoice.customizationOptionData1 | static_cast<u32>(unitRaceCustomizationChoice.customizationOptionData2) << 16;

            if (unitRaceCustomizationChoice.customizationGeosetID > 0 && unitCustomizationGeosetStorage->Has(unitRaceCustomizationChoice.customizationGeosetID))
            {
                unitCustomizationSingleton.choiceIDToGeosetID[id] = unitRaceCustomizationChoice.customizationGeosetID;
            }

            if (unitRaceCustomizationChoice.customizationMaterialID1 > 0 && unitCustomizationMaterialStorage->Has(unitRaceCustomizationChoice.customizationMaterialID1))
            {
                const auto& customizationMaterial = unitCustomizationMaterialStorage->Get<Generated::UnitCustomizationMaterialRecord>(unitRaceCustomizationChoice.customizationMaterialID1);
                unitCustomizationSingleton.choiceIDToTextureSectionID1[id] = customizationMaterial.textureSection;

                if (textureSingleton.materialResourcesIDToTextureHashes.contains(customizationMaterial.materialResourcesID))
                {
                    const auto& textureHashes = textureSingleton.materialResourcesIDToTextureHashes[customizationMaterial.materialResourcesID];
                    unitCustomizationSingleton.choiceIDToTextureHash1[id] = textureHashes[0];
                }
            }

            if (unitRaceCustomizationChoice.customizationMaterialID2 > 0 && unitCustomizationMaterialStorage->Has(unitRaceCustomizationChoice.customizationMaterialID2))
            {
                const auto& customizationMaterial = unitCustomizationMaterialStorage->Get<Generated::UnitCustomizationMaterialRecord>(unitRaceCustomizationChoice.customizationMaterialID2);
                unitCustomizationSingleton.choiceIDToTextureSectionID2[id] = customizationMaterial.textureSection;

                if (textureSingleton.materialResourcesIDToTextureHashes.contains(customizationMaterial.materialResourcesID))
                {
                    const auto& textureHashes = textureSingleton.materialResourcesIDToTextureHashes[customizationMaterial.materialResourcesID];
                    unitCustomizationSingleton.choiceIDToTextureHash2[id] = textureHashes[0];
                }
            }

            if (unitRaceCustomizationChoice.customizationMaterialID3 > 0 && unitCustomizationMaterialStorage->Has(unitRaceCustomizationChoice.customizationMaterialID3))
            {
                const auto& customizationMaterial = unitCustomizationMaterialStorage->Get<Generated::UnitCustomizationMaterialRecord>(unitRaceCustomizationChoice.customizationMaterialID3);
                unitCustomizationSingleton.choiceIDToTextureSectionID3[id] = customizationMaterial.textureSection;

                if (textureSingleton.materialResourcesIDToTextureHashes.contains(customizationMaterial.materialResourcesID))
                {
                    const auto& textureHashes = textureSingleton.materialResourcesIDToTextureHashes[customizationMaterial.materialResourcesID];
                    unitCustomizationSingleton.choiceIDToTextureHash3[id] = textureHashes[0];
                }
            }

            return true;
        });

        static std::string textureFolderPath = "Data/Texture";
        size_t textureFolderSubStr = textureFolderPath.length() + 1; // + 1 here for folder seperator

        unitCustomizationSingleton.unitCustomizationKeyToTextureHash.clear();
        unitCustomizationSingleton.unitCustomizationKeyToTextureHash.reserve(numUnitRaceCustomizationChoiceRows * 3);

        unitCustomizationSingleton.unitCustomizationKeyToTextureID.clear();
        unitCustomizationSingleton.unitCustomizationKeyToTextureID.reserve(numUnitRaceCustomizationChoiceRows * 3);

        std::string textureStrs[3] = { "", "", "" };
        textureStrs[0].reserve(256);
        textureStrs[1].reserve(256);
        textureStrs[2].reserve(256);

        std::string extensionStrs[3] = { "", "", "" };
        extensionStrs[0].reserve(16);
        extensionStrs[1].reserve(16);
        extensionStrs[2].reserve(16);

        for (u32 customizationOptionIndex = (u32)Database::Unit::CustomizationOption::Start; customizationOptionIndex < (u32)Database::Unit::CustomizationOption::Count; customizationOptionIndex++)
        {
            auto customizationOption = static_cast<Database::Unit::CustomizationOption>(customizationOptionIndex);
            const auto& unitCustomizationOption = unitCustomizationOptionStorage->Get<Generated::UnitCustomizationOptionRecord>(customizationOptionIndex);
            
            auto unitCustomizationFlags = *reinterpret_cast<const Database::Unit::UnitCustomizationOptionFlags*>(&unitCustomizationOption.flags);
            if (unitCustomizationFlags.isGeosetOption)
                continue;

            for (u32 raceIndex = (u32)GameDefine::UnitRace::Start; raceIndex <= (u32)GameDefine::UnitRace::Count; raceIndex++)
            {
                auto unitRace = static_cast<GameDefine::UnitRace>(raceIndex);

                for (u32 genderIndex = (u32)GameDefine::UnitGender::Start; genderIndex <= (u32)GameDefine::UnitGender::Female; genderIndex++)
                {
                    auto gender = static_cast<GameDefine::UnitGender>(genderIndex);

                    u32 baseCustomizationOptionKey = CreateCustomizationKey(unitRace, gender, customizationOption);
                    if (!unitCustomizationSingleton.unitBaseCustomizationKeyToChoiceIDList.contains(baseCustomizationOptionKey))
                        continue;

                    const auto& choiceIDList = unitCustomizationSingleton.unitBaseCustomizationKeyToChoiceIDList[baseCustomizationOptionKey];

                    for (u32 choiceID : choiceIDList)
                    {
                        const auto& unitCustomizationChoice = unitRaceCustomizationChoiceStorage->Get<Generated::UnitRaceCustomizationChoiceRecord>(choiceID);

                        bool choiceHasTexture[3] = { unitCustomizationSingleton.choiceIDToTextureHash1.contains(choiceID), unitCustomizationSingleton.choiceIDToTextureHash2.contains(choiceID), unitCustomizationSingleton.choiceIDToTextureHash3.contains(choiceID) };
                        if (!choiceHasTexture[0] && !choiceHasTexture[1] && !choiceHasTexture[2])
                            continue;

                        ParsedTextureName parsedTextureNames[3];
                        u8 choiceIndexKeyOverride = 255;
                        u8 numOptionChoices = 1;
                        u8 numOptionVariants = 1;

                        if (unitCustomizationFlags.isBaseSkin)
                        {
                            numOptionChoices = 1;
                            numOptionVariants = static_cast<u8>(unitCustomizationChoice.customizationOptionData1);
                        }
                        else if (unitCustomizationFlags.isBaseSkinOption)
                        {
                            numOptionChoices = static_cast<u8>(unitCustomizationChoice.customizationOptionData1);
                            numOptionVariants = static_cast<u8>(unitCustomizationChoice.customizationOptionData2);
                        }
                        else if (unitCustomizationFlags.isHairTextureColorOption)
                        {
                            numOptionChoices = 1;
                            numOptionVariants = static_cast<u8>(unitCustomizationChoice.customizationOptionData1);
                            choiceIndexKeyOverride = static_cast<u8>(unitCustomizationChoice.customizationOptionData2);
                        }

                        if (choiceHasTexture[0])
                        {
                            u32 textureHash = unitCustomizationSingleton.choiceIDToTextureHash1[choiceID];
                            const std::string& texturePath = ECSUtil::Texture::GetTexturePath(textureSingleton, textureHash);
                            std::filesystem::path baseTexturePath = std::filesystem::path(texturePath);

                            extensionStrs[0] = baseTexturePath.extension().string();
                            textureStrs[0] = baseTexturePath.string().substr(textureFolderSubStr);
                            std::replace(textureStrs[0].begin(), textureStrs[0].end(), '\\', '/');

                            if (!ParseFileName(textureStrs[0], parsedTextureNames[0]))
                            {
                                NC_LOG_ERROR("Unit Customization Failed to Parse Texture \"{0}\"", textureStrs[0].c_str());
                                choiceHasTexture[0] = false;
                            }
                        }

                        if (choiceHasTexture[1])
                        {
                            u32 textureHash = unitCustomizationSingleton.choiceIDToTextureHash2[choiceID];
                            const std::string& texturePath = ECSUtil::Texture::GetTexturePath(textureSingleton, textureHash);
                            std::filesystem::path baseTexturePath = std::filesystem::path(texturePath);

                            extensionStrs[1] = baseTexturePath.extension().string();
                            textureStrs[1] = baseTexturePath.string().substr(textureFolderSubStr);
                            std::replace(textureStrs[1].begin(), textureStrs[1].end(), '\\', '/');

                            if (!ParseFileName(textureStrs[1], parsedTextureNames[1]))
                            {
                                NC_LOG_ERROR("Unit Customization Failed to Parse Texture \"{0}\"", textureStrs[1].c_str());
                                choiceHasTexture[1] = false;
                            }
                        }

                        if (choiceHasTexture[2])
                        {
                            u32 textureHash = unitCustomizationSingleton.choiceIDToTextureHash3[choiceID];
                            const std::string& texturePath = ECSUtil::Texture::GetTexturePath(textureSingleton, textureHash);
                            std::filesystem::path baseTexturePath = std::filesystem::path(texturePath);

                            extensionStrs[2] = baseTexturePath.extension().string();
                            textureStrs[2] = baseTexturePath.string().substr(textureFolderSubStr);
                            std::replace(textureStrs[2].begin(), textureStrs[2].end(), '\\', '/');

                            if (!ParseFileName(textureStrs[2], parsedTextureNames[2]))
                            {
                                NC_LOG_ERROR("Unit Customization Failed to Parse Texture \"{0}\"", textureStrs[2].c_str());
                                choiceHasTexture[2] = false;
                            }
                        }

                        if (!choiceHasTexture[0] && !choiceHasTexture[1] && !choiceHasTexture[2])
                            continue;

                        for (u8 choiceIndex = 0; choiceIndex < numOptionChoices; choiceIndex++)
                        {
                            for (u32 textureIndex = 0; textureIndex < 3; textureIndex++)
                            {
                                if (!choiceHasTexture[textureIndex])
                                    continue;

                                const auto& parsedTextureName = parsedTextureNames[textureIndex];
                                std::string& textureStr = textureStrs[textureIndex];
                                u32 baseOffset = parsedTextureName.typeStrOffset;

                                u16 modifiedChoiceIndex = choiceIndex + parsedTextureName.typeStartValue;
                                u8 choiceOnes = modifiedChoiceIndex % 10;
                                u8 choiceTens = modifiedChoiceIndex / 10;

                                textureStr[baseOffset] = '0' + choiceTens;
                                textureStr[baseOffset + 1] = '0' + choiceOnes;
                            }

                            for (u8 variationIndex = 0; variationIndex < numOptionVariants; variationIndex++)
                            {
                                for (u32 textureIndex = 0; textureIndex < 3; textureIndex++)
                                {
                                    if (!choiceHasTexture[textureIndex])
                                        continue;

                                    const auto& parsedTextureName = parsedTextureNames[textureIndex];
                                    std::string& textureStr = textureStrs[textureIndex];
                                    u32 baseOffset = parsedTextureName.variationStrOffset;

                                    u16 modifiedVariationIndex = variationIndex + parsedTextureName.variationStartValue;
                                    u8 variationOnes = modifiedVariationIndex % 10;
                                    u8 variationTens = modifiedVariationIndex / 10;
                                    u8 variationHundreds = modifiedVariationIndex / 100;

                                    if (parsedTextureName.variationDigitLength == 2)
                                    {
                                        textureStr[baseOffset] = '0' + variationTens;
                                        textureStr[baseOffset + 1] = '0' + variationOnes;
                                    }
                                    else if (parsedTextureName.variationDigitLength == 3)
                                    {
                                        textureStr[baseOffset] = '0' + variationHundreds;
                                        textureStr[baseOffset + 1] = '0' + variationTens;
                                        textureStr[baseOffset + 2] = '0' + variationOnes;
                                    }

                                    u32 textureHash = StringUtils::fnv1a_32(textureStr.c_str(), textureStr.size());
                                    if (!textureSingleton.textureHashToPath.contains(textureHash))
                                    {
                                        NC_LOG_ERROR("Unit Customization Failed to find Texture \"{0}\"", textureStr.c_str());
                                        continue;
                                    }

                                    u16 modifiedChoiceIndex = choiceIndexKeyOverride == 255 ? choiceIndex + parsedTextureName.typeStartValue : choiceIndexKeyOverride;
                                    u32 customizationKey = CreateCustomizationKey(unitRace, gender, customizationOption, static_cast<u8>(modifiedChoiceIndex), static_cast<u8>(modifiedVariationIndex), textureIndex);
                                    unitCustomizationSingleton.unitCustomizationKeyToTextureHash[customizationKey] = textureHash;
                                }
                            }
                        }
                    }
                }
            }
        }

        for (const auto& pair : unitCustomizationSingleton.unitCustomizationKeyToTextureHash)
        {
            u32 key = pair.first;
            u32 textureHash = pair.second;

            Renderer::TextureDesc textureDesc =
            {
                .path = ECSUtil::Texture::GetTexturePath(textureSingleton, textureHash)
            };

            Renderer::TextureID texture = ServiceLocator::GetGameRenderer()->GetRenderer()->LoadTexture(textureDesc);
            unitCustomizationSingleton.unitCustomizationKeyToTextureID[key] = texture;
        }

        return true;
    }

    bool GetBaseSkinTextureID(ECS::Singletons::UnitCustomizationSingleton& unitCustomizationSingleton, GameDefine::UnitRace race, GameDefine::UnitGender gender, u8 skinID, Renderer::TextureID& textureID)
    {
        u32 customizationKey = CreateCustomizationKey(race, gender, Database::Unit::CustomizationOption::Skin, 0, skinID, 0);
        if (!unitCustomizationSingleton.unitCustomizationKeyToTextureID.contains(customizationKey))
            return false;

        textureID = unitCustomizationSingleton.unitCustomizationKeyToTextureID[customizationKey];
        return true;
    }
    
    bool GetBaseSkinBraTextureID(ECS::Singletons::UnitCustomizationSingleton& unitCustomizationSingleton, GameDefine::UnitRace race, GameDefine::UnitGender gender, u8 skinID, Renderer::TextureID& textureID)
    {
        u32 customizationKey = CreateCustomizationKey(race, gender, Database::Unit::CustomizationOption::SkinBra, 0, skinID, 0);
        if (!unitCustomizationSingleton.unitCustomizationKeyToTextureID.contains(customizationKey))
            return false;

        textureID = unitCustomizationSingleton.unitCustomizationKeyToTextureID[customizationKey];
        return true;
    }

    bool GetBaseSkinUnderwearTextureID(ECS::Singletons::UnitCustomizationSingleton& unitCustomizationSingleton, GameDefine::UnitRace race, GameDefine::UnitGender gender, u8 skinID, Renderer::TextureID& textureID)
    {
        u32 customizationKey = CreateCustomizationKey(race, gender, Database::Unit::CustomizationOption::SkinUnderwear, 0, skinID, 0);
        if (!unitCustomizationSingleton.unitCustomizationKeyToTextureID.contains(customizationKey))
            return false;

        textureID = unitCustomizationSingleton.unitCustomizationKeyToTextureID[customizationKey];
        return true;
    }

    bool GetBaseSkinFaceTextureID(ECS::Singletons::UnitCustomizationSingleton& unitCustomizationSingleton, GameDefine::UnitRace race, GameDefine::UnitGender gender, u8 skinID, u8 faceID, u8 variant, Renderer::TextureID& textureID)
    {
        u32 customizationKey = CreateCustomizationKey(race, gender, Database::Unit::CustomizationOption::Face, faceID, skinID, variant);
        if (!unitCustomizationSingleton.unitCustomizationKeyToTextureID.contains(customizationKey))
            return false;

        textureID = unitCustomizationSingleton.unitCustomizationKeyToTextureID[customizationKey];
        return true;
    }

    bool GetBaseSkinHairTextureID(ECS::Singletons::UnitCustomizationSingleton& unitCustomizationSingleton, GameDefine::UnitRace race, GameDefine::UnitGender gender, u8 hairStyle, u8 hairColor, u8 variant, Renderer::TextureID& textureID)
    {
        u32 choiceID;
        if (!ECSUtil::UnitCustomization::GetChoiceIDFromOptionValue(unitCustomizationSingleton, race, gender, Database::Unit::CustomizationOption::Hairstyle, hairStyle, choiceID))
            return false;

        if (!unitCustomizationSingleton.choiceIDToOptionData.contains(choiceID))
            return false;

        u32 choiceData = unitCustomizationSingleton.choiceIDToOptionData[choiceID];
        u8 hairTextureStyle = choiceData & 0xFFFF;

        u32 customizationKey = CreateCustomizationKey(race, gender, Database::Unit::CustomizationOption::HairColor, hairTextureStyle, hairColor, variant);
        if (!unitCustomizationSingleton.unitCustomizationKeyToTextureID.contains(customizationKey))
            return false;

        textureID = unitCustomizationSingleton.unitCustomizationKeyToTextureID[customizationKey];
        return true;
    }

    void WriteBaseSkin(ECS::Singletons::ClientDBSingleton& clientDBSingleton, ECS::Singletons::UnitCustomizationSingleton& unitCustomizationSingleton, Renderer::TextureID skinTextureID, Renderer::TextureID baseSkinTextureID, vec2 srcMin, vec2 srcMax)
    {
        WriteTextureToSkin(clientDBSingleton, unitCustomizationSingleton, skinTextureID, baseSkinTextureID, Database::Unit::TextureSectionType::FullBody, srcMin, srcMax);
    }

    void WriteTextureToSkin(ECS::Singletons::ClientDBSingleton& clientDBSingleton, ECS::Singletons::UnitCustomizationSingleton& unitCustomizationSingleton, Renderer::TextureID skinTextureID, Renderer::TextureID textureID, Database::Unit::TextureSectionType textureSectionType, vec2 srcMin, vec2 srcMax)
    {
        TextureRenderer* textureRenderer = ServiceLocator::GetGameRenderer()->GetTextureRenderer();

        auto* unitTextureSectionStorage = clientDBSingleton.Get(ClientDBHash::UnitTextureSection);
        u32 textureSectionID = unitCustomizationSingleton.unitTextureSectionTypeToID[textureSectionType];
        const auto& textureSection = unitTextureSectionStorage->Get<Generated::UnitTextureSectionRecord>(textureSectionID);

        static f32 skinTextureSize = 512.0f;
        vec2 destMin = vec2(textureSection.position.x, textureSection.position.y) / skinTextureSize;
        vec2 destMax = vec2(textureSection.position.x + textureSection.size.x, textureSection.position.y + textureSection.size.y) / skinTextureSize;

        textureRenderer->RequestRenderTextureToTexture(skinTextureID, destMin, destMax, textureID, srcMin, srcMax);
    }

    void WriteItemToSkin(ECS::Singletons::TextureSingleton& textureSingleton, ECS::Singletons::ClientDBSingleton& clientDBSingleton, ECS::Singletons::ItemSingleton& itemSingleton, ECS::Singletons::UnitCustomizationSingleton& unitCustomizationSingleton, ECS::Components::UnitCustomization& unitCustomization, u32 itemDisplayID)
    {
        TextureRenderer* textureRenderer = ServiceLocator::GetGameRenderer()->GetTextureRenderer();

        if (!itemSingleton.itemDisplayInfoToComponentSectionData.contains(itemDisplayID))
            return;

        auto* itemDisplayMaterialStorage = clientDBSingleton.Get(ClientDBHash::ItemDisplayMaterialResources);

        const auto& componentSectionData = itemSingleton.itemDisplayInfoToComponentSectionData[itemDisplayID];
        for (const auto& pair : componentSectionData.componentSectionToTextureHash)
        {
            auto componentSection = static_cast<Database::Unit::TextureSectionType>(pair.first);
            u32 textureHash = pair.second;

            Renderer::TextureID textureID = Renderer::TextureID::Invalid();
            if (unitCustomizationSingleton.itemTextureHashToTextureID.contains(textureHash))
            {
                textureID = unitCustomizationSingleton.itemTextureHashToTextureID[textureHash];
            }
            else
            {
                const auto& texturePath = textureSingleton.textureHashToPath[textureHash];
                Renderer::TextureDesc textureDesc =
                {
                    .path = texturePath
                };

                textureID = ServiceLocator::GetGameRenderer()->GetRenderer()->LoadTexture(textureDesc);
                unitCustomizationSingleton.itemTextureHashToTextureID[textureHash] = textureID;
            }

            u16 componentSectionsInUseBits = *reinterpret_cast<u16*>(&unitCustomization.componentSectionsInUse);
            componentSectionsInUseBits |= 1 << static_cast<u16>(componentSection);

            unitCustomization.componentSectionsInUse = *reinterpret_cast<ECS::Components::UnitComponentSectionsInUse*>(&componentSectionsInUseBits);
            WriteTextureToSkin(clientDBSingleton, unitCustomizationSingleton, unitCustomization.skinTextureID, textureID, componentSection);
        }
    }

    bool GetChoiceIDFromOptionValue(ECS::Singletons::UnitCustomizationSingleton& unitCustomizationSingleton, GameDefine::UnitRace race, GameDefine::UnitGender gender, Database::Unit::CustomizationOption customizationOption, u8 customizationOptionValue, u32& choiceID)
    {
        u32 baseCustomizationKey = CreateCustomizationKey(race, gender, customizationOption);
        if (!unitCustomizationSingleton.unitBaseCustomizationKeyToChoiceIDList.contains(baseCustomizationKey))
            return false;

        auto& choiceIDList = unitCustomizationSingleton.unitBaseCustomizationKeyToChoiceIDList[baseCustomizationKey];
        if (customizationOptionValue >= choiceIDList.size())
            return false;

        choiceID = choiceIDList[customizationOptionValue];
        return true;
    }
    bool GetGeosetFromOptionValue(ECS::Singletons::UnitCustomizationSingleton& unitCustomizationSingleton, GameDefine::UnitRace race, GameDefine::UnitGender gender, Database::Unit::CustomizationOption customizationOption, u8 customizationOptionValue, u16& geoset)
    {
        u32 baseCustomizationKey = CreateCustomizationKey(race, gender, customizationOption);
        if (!unitCustomizationSingleton.unitBaseCustomizationKeyToChoiceIDList.contains(baseCustomizationKey))
            return false;

        auto& choiceIDList = unitCustomizationSingleton.unitBaseCustomizationKeyToChoiceIDList[baseCustomizationKey];
        if (customizationOptionValue >= choiceIDList.size())
            return false;

        u32 choiceID = choiceIDList[customizationOptionValue];
        if (!unitCustomizationSingleton.choiceIDToGeosetID.contains(choiceID))
            return false;

        u32 geosetID = unitCustomizationSingleton.choiceIDToGeosetID[choiceID];
        if (!unitCustomizationSingleton.geosetIDToGeosetKey.contains(geosetID))
            return false;

        geoset = unitCustomizationSingleton.geosetIDToGeosetKey[geosetID];
        return true;
    }

    bool GetHairTexture(ECS::Singletons::UnitCustomizationSingleton& unitCustomizationSingleton, GameDefine::UnitRace race, GameDefine::UnitGender gender, u8 hairStyle, u8 hairColor, Renderer::TextureID& textureID)
    {
        u32 choiceID;
        if (!ECSUtil::UnitCustomization::GetChoiceIDFromOptionValue(unitCustomizationSingleton, race, gender, Database::Unit::CustomizationOption::Hairstyle, hairStyle, choiceID))
            return false;

        if (!unitCustomizationSingleton.choiceIDToOptionData.contains(choiceID))
            return false;

        u32 choiceData = unitCustomizationSingleton.choiceIDToOptionData[choiceID];
        u8 hairTextureStyle = choiceData & 0xFFFF;

        u32 customizationKey = CreateCustomizationKey(race, gender, Database::Unit::CustomizationOption::HairColor, hairTextureStyle, hairColor, 0);
        if (!unitCustomizationSingleton.unitCustomizationKeyToTextureID.contains(customizationKey))
            return false;

        textureID = unitCustomizationSingleton.unitCustomizationKeyToTextureID[customizationKey];
        return true;
    }

    u32 CreateCustomizationKey(GameDefine::UnitRace race, GameDefine::UnitGender gender, Database::Unit::CustomizationOption customizationOption, u8 choiceIndex, u8 variationIndex, u8 materialIndex)
    {
        // Key (Race, Gender, Customization Option, ChoiceIndex, VariantIndex, TextureIndex)
        // Race 6 Bits (64 Values)
        // Gender 2 Bits (4 Values)
        // Customization Option 6 Bits (64 Values)
        // Choice Index 8 Bits (256 Values)
        // Variant Index 8 Bits (256 Values)
        // Material Index 2 Bits (4 Values)
        // Total Bits 32 Bits

        u32 raceID = static_cast<u32>(race) & 0x3F;
        u32 genderID = (static_cast<u32>(gender) - 1) & 0x3;
        u32 customizationOptionID = static_cast<u32>(customizationOption) & 0x3F;
        u32 choiceIndexID = choiceIndex & 0xFF;
        u32 variationIndexID = variationIndex & 0xFF;
        u32 materialIndexID = materialIndex & 0x3;

        u32 key = raceID | (genderID << 6) | (customizationOptionID << 8) | (choiceIndexID << 14) | (variationIndexID << 22) | (materialIndexID << 30);
        return key;
    }
}
