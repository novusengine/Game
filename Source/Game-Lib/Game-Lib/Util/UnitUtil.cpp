#include "UnitUtil.h"

#include "Game-Lib/ECS/Components/AABB.h"
#include "Game-Lib/ECS/Components/DisplayInfo.h"
#include "Game-Lib/ECS/Components/Model.h"
#include "Game-Lib/ECS/Components/Name.h"
#include "Game-Lib/ECS/Components/Unit.h"
#include "Game-Lib/ECS/Components/UnitCustomization.h"
#include "Game-Lib/ECS/Components/UnitEquipment.h"
#include "Game-Lib/ECS/Components/UnitPowersComponent.h"
#include "Game-Lib/ECS/Components/UnitResistancesComponent.h"
#include "Game-Lib/ECS/Components/UnitStatsComponent.h"
#include "Game-Lib/ECS/Singletons/Database/ClientDBSingleton.h"
#include "Game-Lib/ECS/Singletons/Database/ItemSingleton.h"
#include "Game-Lib/ECS/Util/Database/UnitCustomizationUtil.h"
#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Rendering/Model/ModelLoader.h"
#include "Game-Lib/Rendering/Texture/TextureRenderer.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <MetaGen/Shared/ClientDB/ClientDB.h>

#include <entt/entt.hpp>

using namespace ECS;

namespace Util::Unit
{
    bool HasPower(const Components::UnitPowersComponent& unitPowersComponent, MetaGen::Shared::Unit::PowerTypeEnum powerType)
    {
        bool hasPowerType = unitPowersComponent.powerTypeToValue.contains(powerType);
        return hasPowerType;
    }
    UnitPower& GetPower(Components::UnitPowersComponent& unitPowersComponent, MetaGen::Shared::Unit::PowerTypeEnum powerType)
    {
        return unitPowersComponent.powerTypeToValue.at(powerType);
    }
    bool AddPower(Components::UnitPowersComponent& unitPowersComponent, MetaGen::Shared::Unit::PowerTypeEnum powerType, f64 base, f64 current, f64 max)
    {
        if (HasPower(unitPowersComponent, powerType))
            return false;

        unitPowersComponent.dirtyPowerTypes.insert(powerType);
        unitPowersComponent.powerTypeToValue[powerType] = UnitPower{
            .base = base,
            .current = current,
            .max = max
        };

        return true;
    }
    bool SetPower(ECS::Components::UnitPowersComponent& unitPowersComponent, MetaGen::Shared::Unit::PowerTypeEnum powerType, f64 base, f64 current, f64 max)
    {
        if (!HasPower(unitPowersComponent, powerType))
            return false;

        UnitPower& power = GetPower(unitPowersComponent, powerType);
        power.base = base;
        power.current = current;
        power.max = max;

        unitPowersComponent.dirtyPowerTypes.insert(powerType);
        return true;
    }

    bool HasResistance(const Components::UnitResistancesComponent& unitResistancesComponent, MetaGen::Shared::Unit::ResistanceTypeEnum resistanceType)
    {
        bool hasResistanceType = unitResistancesComponent.resistanceTypeToValue.contains(resistanceType);
        return hasResistanceType;
    }
    UnitResistance& GetResistance(Components::UnitResistancesComponent& unitResistancesComponent, MetaGen::Shared::Unit::ResistanceTypeEnum resistanceType)
    {
        return unitResistancesComponent.resistanceTypeToValue.at(resistanceType);
    }
    bool AddResistance(Components::UnitResistancesComponent& unitResistancesComponent, MetaGen::Shared::Unit::ResistanceTypeEnum resistanceType, f64 base, f64 current, f64 max)
    {
        if (HasResistance(unitResistancesComponent, resistanceType))
            return false;

        unitResistancesComponent.dirtyResistanceTypes.insert(resistanceType);
        unitResistancesComponent.resistanceTypeToValue[resistanceType] = UnitResistance{
            .base = base,
            .current = current,
            .max = max
        };

        return true;
    }
    bool SetResistance(ECS::Components::UnitResistancesComponent& unitResistancesComponent, MetaGen::Shared::Unit::ResistanceTypeEnum resistanceType, f64 base, f64 current, f64 max)
    {
        if (!HasResistance(unitResistancesComponent, resistanceType))
            return false;

        UnitResistance& resistance = GetResistance(unitResistancesComponent, resistanceType);
        resistance.base = base;
        resistance.current = current;
        resistance.max = max;

        unitResistancesComponent.dirtyResistanceTypes.insert(resistanceType);
        return true;
    }

    bool HasStat(const Components::UnitStatsComponent& unitStatsComponent, MetaGen::Shared::Unit::StatTypeEnum statType)
    {
        bool hasStatType = unitStatsComponent.statTypeToValue.contains(statType);
        return hasStatType;
    }
    UnitStat& GetStat(Components::UnitStatsComponent& unitStatsComponent, MetaGen::Shared::Unit::StatTypeEnum statType)
    {
        return unitStatsComponent.statTypeToValue.at(statType);
    }
    bool AddStat(Components::UnitStatsComponent& unitStatsComponent, MetaGen::Shared::Unit::StatTypeEnum statType, f64 base, f64 current)
    {
        if (HasStat(unitStatsComponent, statType))
            return false;

        unitStatsComponent.dirtyStatTypes.insert(statType);
        unitStatsComponent.statTypeToValue[statType] = UnitStat{
            .base = base,
            .current = current
        };

        return true;
    }
    bool SetStat(ECS::Components::UnitStatsComponent& unitStatsComponent, MetaGen::Shared::Unit::StatTypeEnum statType, f64 base, f64 current)
    {
        if (!HasStat(unitStatsComponent, statType))
            return false;

        UnitStat& stat = GetStat(unitStatsComponent, statType);
        stat.base = base;
        stat.current = current;
        stat.current = current;

        unitStatsComponent.dirtyStatTypes.insert(statType);
        return true;
    }

    void EnableGeometryGroup(entt::registry& registry, entt::entity entity, const ::ECS::Components::Model& model, u32 groupID)
    {
        if (model.flags.loaded)
        {
            auto* modelLoader = ServiceLocator::GetGameRenderer()->GetModelLoader();
            modelLoader->EnableGroupForEntity(entity, groupID);
        }
        else
        {
            auto& modelQueuedGeometryGroups = registry.get_or_emplace<Components::ModelQueuedGeometryGroups>(entity);
            modelQueuedGeometryGroups.enabledGroupIDs.insert(groupID);
        }
    }

    void DisableGeometryGroups(entt::registry& registry, entt::entity entity, const ::ECS::Components::Model& model, u32 startGroupID, u32 endGroupID)
    {
        if (model.flags.loaded)
        {
            auto* modelLoader = ServiceLocator::GetGameRenderer()->GetModelLoader();
            modelLoader->DisableGroupsForEntity(entity, startGroupID, endGroupID);
        }
        else
        {
            auto& modelQueuedGeometryGroups = registry.get_or_emplace<Components::ModelQueuedGeometryGroups>(entity);

            if (endGroupID == 0)
            {
                modelQueuedGeometryGroups.enabledGroupIDs.erase(startGroupID);
            }
            else
            {
                for (auto it = modelQueuedGeometryGroups.enabledGroupIDs.begin(); it != modelQueuedGeometryGroups.enabledGroupIDs.end();)
                {
                    u32 groupID = *it;
                    if (groupID >= startGroupID && groupID <= endGroupID)
                    {
                        it = modelQueuedGeometryGroups.enabledGroupIDs.erase(it);
                    }
                    else
                    {
                        ++it;
                    }
                }
            }
        }
    }

    void DisableAllGeometryGroups(entt::registry& registry, entt::entity entity, const ::ECS::Components::Model& model)
    {
        if (model.flags.loaded)
        {
            auto* modelLoader = ServiceLocator::GetGameRenderer()->GetModelLoader();
            modelLoader->DisableAllGroupsForEntity(entity);
        }
        else
        {
            auto& modelQueuedGeometryGroups = registry.get_or_emplace<Components::ModelQueuedGeometryGroups>(entity);
            modelQueuedGeometryGroups.enabledGroupIDs.clear();
        }
    }

    void RefreshGeometryGroups(entt::registry& registry, entt::entity entity, ECS::Singletons::ClientDBSingleton& clientDBSingleton, ECS::Singletons::UnitCustomizationSingleton& unitCustomizationSingleton, const ::ECS::Components::Model& model)
    {
        auto* itemStorage = clientDBSingleton.Get(ClientDBHash::Item);
        auto* itemDisplayInfoStorage = clientDBSingleton.Get(ClientDBHash::ItemDisplayInfo);

        auto& displayInfo = registry.get<::ECS::Components::DisplayInfo>(entity);
        auto& unitEquipment = registry.get<::ECS::Components::UnitEquipment>(entity);
        auto& unitCustomization = registry.get<::ECS::Components::UnitCustomization>(entity);

        {
            // Hair
            {
                DisableGeometryGroups(registry, entity, model, 1, 99);

                u16 hairGeosetID;
                if (ECSUtil::UnitCustomization::GetGeosetFromOptionValue(unitCustomizationSingleton, displayInfo.race, displayInfo.gender, Database::Unit::CustomizationOption::Hairstyle, unitCustomization.hairStyleID, hairGeosetID))
                {
                    EnableGeometryGroup(registry, entity, model, hairGeosetID);
                }
                else
                {
                    EnableGeometryGroup(registry, entity, model, 1);
                }
            }

            // Piercings
            {
                u16 piercingGeosetID;
                if (ECSUtil::UnitCustomization::GetGeosetFromOptionValue(unitCustomizationSingleton, displayInfo.race, displayInfo.gender, Database::Unit::CustomizationOption::Piercings, unitCustomization.piercingsID, piercingGeosetID))
                {
                    DisableGeometryGroups(registry, entity, model, 301, 399);
                    EnableGeometryGroup(registry, entity, model, piercingGeosetID);
                }
            }
        }

        auto GetItemDisplayID = [&itemStorage, &itemDisplayInfoStorage, &unitEquipment](Database::Item::ItemEquipSlot equipSlot) -> u32
        {
            u32 itemID = unitEquipment.equipmentSlotToVisualItemID[(u32)equipSlot];
            if (itemID == 0)
                return 0;

            const auto& itemInfo = itemStorage->Get<::MetaGen::Shared::ClientDB::ItemRecord>(itemID);
            return itemInfo.displayID;
        };

        u32 shirtDisplayID = GetItemDisplayID(Database::Item::ItemEquipSlot::Shirt);
        u32 chestDisplayID = GetItemDisplayID(Database::Item::ItemEquipSlot::Chest);
        u32 beltDisplayID = GetItemDisplayID(Database::Item::ItemEquipSlot::Belt);
        u32 pantsDisplayID = GetItemDisplayID(Database::Item::ItemEquipSlot::Pants);
        u32 bootsDisplayID = GetItemDisplayID(Database::Item::ItemEquipSlot::Boots);
        u32 glovesDisplayID = GetItemDisplayID(Database::Item::ItemEquipSlot::Gloves);
        u32 tabardDisplayID = GetItemDisplayID(Database::Item::ItemEquipSlot::Tabard);
        u32 cloakDisplayID = GetItemDisplayID(Database::Item::ItemEquipSlot::Cloak);

        // Default Geometry
        ::Util::Unit::EnableGeometryGroup(registry, entity, model, 101);
        ::Util::Unit::EnableGeometryGroup(registry, entity, model, 201);
        ::Util::Unit::EnableGeometryGroup(registry, entity, model, 301);
        ::Util::Unit::EnableGeometryGroup(registry, entity, model, 401);
        ::Util::Unit::EnableGeometryGroup(registry, entity, model, 501);
        ::Util::Unit::EnableGeometryGroup(registry, entity, model, 702);
        ::Util::Unit::EnableGeometryGroup(registry, entity, model, 1101);
        ::Util::Unit::EnableGeometryGroup(registry, entity, model, 1301);
        ::Util::Unit::EnableGeometryGroup(registry, entity, model, 1501);

        unitCustomization.flags.hasGloveModel = false;

        if (glovesDisplayID > 0)
        {
            auto* glovesDisplayInfo = itemDisplayInfoStorage->TryGet<MetaGen::Shared::ClientDB::ItemDisplayInfoRecord>(glovesDisplayID);
            if (glovesDisplayInfo && glovesDisplayInfo->modelGeosetGroups[0])
            {
                DisableGeometryGroups(registry, entity, model, 401, 499);

                u32 geometryGroupID = 401 + glovesDisplayInfo->modelGeosetGroups[0];
                EnableGeometryGroup(registry, entity, model, geometryGroupID);

                unitCustomization.flags.hasGloveModel = true;
            }
        }
        
        bool chestEnabled800 = false;
        if (!unitCustomization.flags.hasGloveModel && chestDisplayID > 0)
        {
            auto* chestDisplayInfo = itemDisplayInfoStorage->TryGet<MetaGen::Shared::ClientDB::ItemDisplayInfoRecord>(chestDisplayID);
            if (chestDisplayInfo && chestDisplayInfo->modelGeosetGroups[0])
            {
                u32 geometryGroupID = 801 + chestDisplayInfo->modelGeosetGroups[0];
                EnableGeometryGroup(registry, entity, model, geometryGroupID);

                chestEnabled800 = true;
            }
        }

        auto* tabardDisplayInfo = itemDisplayInfoStorage->TryGet<MetaGen::Shared::ClientDB::ItemDisplayInfoRecord>(tabardDisplayID);
        if (tabardDisplayInfo)
        {
            if (!(tabardDisplayInfo->flags & 0x100000))
            {
                DisableGeometryGroups(registry, entity, model, 2200, 2299);
                EnableGeometryGroup(registry, entity, model, 2202);
            }
        }
        else if (chestDisplayID > 0)
        {
            auto* chestDisplayInfo = itemDisplayInfoStorage->TryGet<MetaGen::Shared::ClientDB::ItemDisplayInfoRecord>(chestDisplayID);
            if (chestDisplayInfo && chestDisplayInfo->modelGeosetGroups[3])
            {
                u32 geometryGroupID = 2201 + chestDisplayInfo->modelGeosetGroups[3];
                DisableGeometryGroups(registry, entity, model, 2200, 2299);
                EnableGeometryGroup(registry, entity, model, geometryGroupID);
            }
        }
        
        unitCustomization.flags.hasBeltModel = false;

        auto* beltDisplayInfo = itemDisplayInfoStorage->TryGet<MetaGen::Shared::ClientDB::ItemDisplayInfoRecord>(beltDisplayID);
        if (beltDisplayInfo)
            unitCustomization.flags.hasBeltModel = beltDisplayInfo->flags & 0x200;

        unitCustomization.flags.hasChestDress = false;
        unitCustomization.flags.hasPantsDress = false;

        if (chestDisplayID > 0)
        {
            auto* chestDisplayInfo = itemDisplayInfoStorage->TryGet<MetaGen::Shared::ClientDB::ItemDisplayInfoRecord>(chestDisplayID);
            if (chestDisplayInfo && chestDisplayInfo->modelGeosetGroups[2])
            {
                unitCustomization.flags.hasChestDress = true;
                unitCustomization.flags.hasPantsDress = false;

                DisableGeometryGroups(registry, entity, model, 501, 599);
                DisableGeometryGroups(registry, entity, model, 902, 999);
                DisableGeometryGroups(registry, entity, model, 1100, 1199);
                DisableGeometryGroups(registry, entity, model, 1300, 1399);

                u32 geometryGroupID = 1301 + chestDisplayInfo->modelGeosetGroups[2];
                EnableGeometryGroup(registry, entity, model, geometryGroupID);
            }
        }
        else if (pantsDisplayID > 0)
        {
            auto* pantsDisplayInfo = itemDisplayInfoStorage->TryGet<MetaGen::Shared::ClientDB::ItemDisplayInfoRecord>(pantsDisplayID);
            if (pantsDisplayInfo && pantsDisplayInfo->modelGeosetGroups[2])
            {
                unitCustomization.flags.hasChestDress = false;
                unitCustomization.flags.hasPantsDress = true;

                DisableGeometryGroups(registry, entity, model, 501, 599);
                DisableGeometryGroups(registry, entity, model, 902, 999);
                DisableGeometryGroups(registry, entity, model, 1100, 1199);
                DisableGeometryGroups(registry, entity, model, 1300, 1399);

                u32 geometryGroupID = 1301 + pantsDisplayInfo->modelGeosetGroups[2];
                EnableGeometryGroup(registry, entity, model, geometryGroupID);
            }
        }

        bool hasDress = unitCustomization.flags.hasChestDress || unitCustomization.flags.hasPantsDress;
        if (!hasDress)
        {
            auto* bootsDisplayInfo = itemDisplayInfoStorage->TryGet<MetaGen::Shared::ClientDB::ItemDisplayInfoRecord>(bootsDisplayID);
            if (bootsDisplayInfo && bootsDisplayInfo->modelGeosetGroups[0])
            {
                DisableGeometryGroups(registry, entity, model, 501, 599);
                EnableGeometryGroup(registry, entity, model, 901);

                u32 geometryGroupID = 501 + bootsDisplayInfo->modelGeosetGroups[0];
                EnableGeometryGroup(registry, entity, model, geometryGroupID);
            }
            else
            {
                auto* pantsDisplayInfo = itemDisplayInfoStorage->TryGet<MetaGen::Shared::ClientDB::ItemDisplayInfoRecord>(pantsDisplayID);
                u32 geometryGroupID = pantsDisplayInfo && pantsDisplayInfo->modelGeosetGroups[1] ? 901 + pantsDisplayInfo->modelGeosetGroups[1] : 901;
                EnableGeometryGroup(registry, entity, model, geometryGroupID);
            }

            if (!unitCustomization.flags.hasGloveModel && !chestEnabled800)
            {
                auto* shirtDisplayInfo = itemDisplayInfoStorage->TryGet<MetaGen::Shared::ClientDB::ItemDisplayInfoRecord>(shirtDisplayID);
                if (shirtDisplayInfo && shirtDisplayInfo->modelGeosetGroups[0])
                {
                    u32 geometryGroupID = 801 + shirtDisplayInfo->modelGeosetGroups[0];
                    EnableGeometryGroup(registry, entity, model, geometryGroupID);
                }
            }
        }

        // Boots
        {
            bool bootsFlag = false;
            auto* bootsDisplayInfo = itemDisplayInfoStorage->TryGet<MetaGen::Shared::ClientDB::ItemDisplayInfoRecord>(bootsDisplayID);
            if (bootsDisplayInfo)
                bootsFlag = (bootsDisplayInfo->flags & 0x100000) == 0;

            u32 bootsGeometryGroupID = 0;
            if (bootsDisplayInfo && bootsDisplayInfo->modelGeosetGroups[1])
            {
                bootsGeometryGroupID = 2000 + bootsDisplayInfo->modelGeosetGroups[1];
            }
            else
            {
                bootsGeometryGroupID = bootsFlag ? 2002 : 2001;
            }
            EnableGeometryGroup(registry, entity, model, bootsGeometryGroupID);
        }

        bool showsTabard = false;
        if (!hasDress && tabardDisplayID > 0 && tabardDisplayInfo && tabardDisplayInfo->modelGeosetGroups[0])
        {
            showsTabard = true;
            u32 geometryGroupID = unitCustomization.flags.hasBeltModel ? 1203 : 1201 + tabardDisplayInfo->modelGeosetGroups[0];
            EnableGeometryGroup(registry, entity, model, geometryGroupID);
        }
        else if (tabardDisplayID > 0)
        {
            u32 geometryGroupID1 = 1201;
            EnableGeometryGroup(registry, entity, model, geometryGroupID1);

            if (!hasDress)
            {
                showsTabard = true;

                u32 geometryGroupID2 = 1202;
                EnableGeometryGroup(registry, entity, model, geometryGroupID2);
            }
        }

        if (!unitCustomization.flags.hasChestDress && !showsTabard)
        {
            if (chestDisplayID)
            {
                auto* chestDisplayInfo = itemDisplayInfoStorage->TryGet<MetaGen::Shared::ClientDB::ItemDisplayInfoRecord>(chestDisplayID);
                if (chestDisplayInfo && chestDisplayInfo->modelGeosetGroups[1])
                {
                    u32 geometryGroupID = 1001 + chestDisplayInfo->modelGeosetGroups[1];
                    EnableGeometryGroup(registry, entity, model, geometryGroupID);
                }
            }
            else if (shirtDisplayID)
            {
                auto* shirtDisplayInfo = itemDisplayInfoStorage->TryGet<MetaGen::Shared::ClientDB::ItemDisplayInfoRecord>(shirtDisplayID);
                if (shirtDisplayInfo && shirtDisplayInfo->modelGeosetGroups[1])
                {
                    u32 geometryGroupID = 1001 + shirtDisplayInfo->modelGeosetGroups[1];
                    EnableGeometryGroup(registry, entity, model, geometryGroupID);
                }
            }
        }

        if (!unitCustomization.flags.hasChestDress)
        {
            if (pantsDisplayID)
            {
                auto* pantsDisplayInfo = itemDisplayInfoStorage->TryGet<MetaGen::Shared::ClientDB::ItemDisplayInfoRecord>(pantsDisplayID);
                if (pantsDisplayInfo && pantsDisplayInfo->modelGeosetGroups[0])
                {
                    u32 geometryGroup = pantsDisplayInfo->modelGeosetGroups[0];
                    u32 geometryGroupID = 1001 + geometryGroup;

                    if (geometryGroup > 2)
                    {
                        DisableGeometryGroups(registry, entity, model, 1300, 1399);
                        EnableGeometryGroup(registry, entity, model, geometryGroupID);
                    }
                    else if (!showsTabard)
                    {
                        EnableGeometryGroup(registry, entity, model, geometryGroupID);
                    }
                }
            }
        }

        if (cloakDisplayID)
        {
            auto* cloakDisplayInfo = itemDisplayInfoStorage->TryGet<MetaGen::Shared::ClientDB::ItemDisplayInfoRecord>(cloakDisplayID);
            if (cloakDisplayInfo && cloakDisplayInfo->modelGeosetGroups[0])
            {
                DisableGeometryGroups(registry, entity, model, 1500, 1599);

                u32 geometryGroupID = 1501 + cloakDisplayInfo->modelGeosetGroups[0];
                EnableGeometryGroup(registry, entity, model, geometryGroupID);
            }
        }

        if (beltDisplayID)
        {
            auto* beltDisplayInfo = itemDisplayInfoStorage->TryGet<MetaGen::Shared::ClientDB::ItemDisplayInfoRecord>(beltDisplayID);
            if (beltDisplayInfo && beltDisplayInfo->modelGeosetGroups[0])
            {
                DisableGeometryGroups(registry, entity, model, 1800, 1899);

                u32 geometryGroupID = 1801 + beltDisplayInfo->modelGeosetGroups[0];
                EnableGeometryGroup(registry, entity, model, geometryGroupID);
            }
        }
    }

    void RefreshSkinTexture(entt::registry& registry, entt::entity entity, ECS::Singletons::ClientDBSingleton& clientDBSingleton, ECS::Singletons::UnitCustomizationSingleton& unitCustomizationSingleton, const ::ECS::Components::DisplayInfo& displayInfo, ::ECS::Components::UnitCustomization& unitCustomization, const ::ECS::Components::Model& model)
    {
        ModelLoader* modelLoader = ServiceLocator::GetGameRenderer()->GetModelLoader();
        auto refreshHairTexture = [&]() {
            if (!unitCustomization.flags.hairChanged && !unitCustomization.flags.forceRefresh)
                return;

            Renderer::TextureID hairTextureID;
            if (ECSUtil::UnitCustomization::GetHairTexture(unitCustomizationSingleton, displayInfo.race, displayInfo.gender, unitCustomization.hairStyleID, unitCustomization.hairColorID, hairTextureID))
                modelLoader->SetHairTextureForEntity(entity, hairTextureID);
            else
                modelLoader->SetHairTextureForEntity(entity, Renderer::TextureID::Invalid());
            unitCustomization.flags.hairChanged = false;
        };

        if (!unitCustomization.flags.useCustomSkin)
        {
            refreshHairTexture();
            unitCustomization.flags.forceRefresh = false;
            return;
        }

        // TODO : Get Customization Info from DisplayID if possible, otherwise use UnitCustomization
        Renderer::TextureID baseSkinTextureID;
        if (!ECSUtil::UnitCustomization::GetBaseSkinTextureID(unitCustomizationSingleton, displayInfo.race, displayInfo.gender, unitCustomization.skinID, baseSkinTextureID))
        {
            return;
        }

        TextureRenderer* textureRenderer = ServiceLocator::GetGameRenderer()->GetTextureRenderer();

        bool justCreatedSkinTexture = false;
        if (unitCustomization.skinTextureID == Renderer::TextureID::Invalid())
        {
            unitCustomization.skinTextureID = textureRenderer->MakeRenderableCopy(baseSkinTextureID, 512, 512);
            justCreatedSkinTexture = true;
        }

        if (justCreatedSkinTexture || unitCustomization.flags.forceRefresh)
        {
            modelLoader->SetSkinTextureForEntity(entity, unitCustomization.skinTextureID);
        }

        if (displayInfo.displayID != 0 && displayInfo.race != GameDefine::UnitRace::None && displayInfo.gender != GameDefine::UnitGender::None)
        {
            if (!justCreatedSkinTexture)
                ECSUtil::UnitCustomization::WriteBaseSkin(clientDBSingleton, unitCustomizationSingleton, unitCustomization.skinTextureID, baseSkinTextureID);

            Renderer::TextureID skinBraTextureID;
            if (ECSUtil::UnitCustomization::GetBaseSkinBraTextureID(unitCustomizationSingleton, displayInfo.race, displayInfo.gender, unitCustomization.skinID, skinBraTextureID))
            {
                ECSUtil::UnitCustomization::WriteTextureToSkin(clientDBSingleton, unitCustomizationSingleton, unitCustomization.skinTextureID, skinBraTextureID, Database::Unit::TextureSectionType::TorsoUpper);
            }

            Renderer::TextureID skinUnderwearTextureID;
            if (ECSUtil::UnitCustomization::GetBaseSkinUnderwearTextureID(unitCustomizationSingleton, displayInfo.race, displayInfo.gender, unitCustomization.skinID, skinUnderwearTextureID))
            {
                ECSUtil::UnitCustomization::WriteTextureToSkin(clientDBSingleton, unitCustomizationSingleton, unitCustomization.skinTextureID, skinUnderwearTextureID, Database::Unit::TextureSectionType::LegUpper);
            }

            Renderer::TextureID skinFaceTextureID1;
            if (ECSUtil::UnitCustomization::GetBaseSkinFaceTextureID(unitCustomizationSingleton, displayInfo.race, displayInfo.gender, unitCustomization.skinID, unitCustomization.faceID, 0, skinFaceTextureID1))
            {
                ECSUtil::UnitCustomization::WriteTextureToSkin(clientDBSingleton, unitCustomizationSingleton, unitCustomization.skinTextureID, skinFaceTextureID1, Database::Unit::TextureSectionType::HeadLower);
            }

            Renderer::TextureID skinFaceTextureID2;
            if (ECSUtil::UnitCustomization::GetBaseSkinFaceTextureID(unitCustomizationSingleton, displayInfo.race, displayInfo.gender, unitCustomization.skinID, unitCustomization.faceID, 1, skinFaceTextureID2))
            {
                ECSUtil::UnitCustomization::WriteTextureToSkin(clientDBSingleton, unitCustomizationSingleton, unitCustomization.skinTextureID, skinFaceTextureID2, Database::Unit::TextureSectionType::HeadUpper);
            }

            Renderer::TextureID skinHairTextureID2;
            if (ECSUtil::UnitCustomization::GetBaseSkinHairTextureID(unitCustomizationSingleton, displayInfo.race, displayInfo.gender, unitCustomization.hairStyleID, unitCustomization.hairColorID, 2, skinHairTextureID2))
            {
                ECSUtil::UnitCustomization::WriteTextureToSkin(clientDBSingleton, unitCustomizationSingleton, unitCustomization.skinTextureID, skinHairTextureID2, Database::Unit::TextureSectionType::HeadUpper);
            }

            Renderer::TextureID skinHairTextureID1;
            if (ECSUtil::UnitCustomization::GetBaseSkinHairTextureID(unitCustomizationSingleton, displayInfo.race, displayInfo.gender, unitCustomization.hairStyleID, unitCustomization.hairColorID, 1, skinHairTextureID1))
            {
                ECSUtil::UnitCustomization::WriteTextureToSkin(clientDBSingleton, unitCustomizationSingleton, unitCustomization.skinTextureID, skinHairTextureID1, Database::Unit::TextureSectionType::HeadLower);
            }

            refreshHairTexture();
        }

        unitCustomization.flags.forceRefresh = false;
    }

}
