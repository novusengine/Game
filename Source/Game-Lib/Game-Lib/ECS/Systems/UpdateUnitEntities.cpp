#include "UpdateUnitEntities.h"

#include "Game-Lib/ECS/Components/DisplayInfo.h"
#include "Game-Lib/ECS/Components/Events.h"
#include "Game-Lib/ECS/Components/Model.h"
#include "Game-Lib/ECS/Components/MovementInfo.h"
#include "Game-Lib/ECS/Components/Name.h"
#include "Game-Lib/ECS/Components/Tags.h"
#include "Game-Lib/ECS/Components/Unit.h"
#include "Game-Lib/ECS/Components/UnitCustomization.h"
#include "Game-Lib/ECS/Components/UnitEquipment.h"
#include "Game-Lib/ECS/Components/UnitPowersComponent.h"
#include "Game-Lib/ECS/Components/UnitResistancesComponent.h"
#include "Game-Lib/ECS/Components/UnitStatsComponent.h"
#include "Game-Lib/ECS/Singletons/CharacterSingleton.h"
#include "Game-Lib/ECS/Singletons/Database/ClientDBSingleton.h"
#include "Game-Lib/ECS/Singletons/Database/UnitCustomizationSingleton.h"
#include "Game-Lib/ECS/Singletons/Database/TextureSingleton.h"
#include "Game-Lib/ECS/Util/RemoteUnitPresentation.h"
#include "Game-Lib/ECS/Util/Transforms.h"
#include "Game-Lib/ECS/Util/Database/TextureUtil.h"
#include "Game-Lib/ECS/Util/Database/UnitCustomizationUtil.h"
#include "Game-Lib/Gameplay/Database/Unit.h"
#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Rendering/Model/ModelLoader.h"
#include "Game-Lib/Rendering/Texture/TextureRenderer.h"
#include "Game-Lib/Util/ServiceLocator.h"
#include "Game-Lib/Util/UnitUtil.h"

#include <MetaGen/Shared/ClientDB/ClientDB.h>

#include <entt/entt.hpp>
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyFilter.h>

#include <tracy/tracy.hpp>

namespace ECS::Systems
{
    class NetworkedEntityFilter : public JPH::BodyFilter
    {
    public:
        NetworkedEntityFilter(u32 bodyID) : _bodyID(bodyID) { }

        bool ShouldCollide(const JPH::BodyID& inBodyID) const override
        {
            return _bodyID != inBodyID;
        }

        bool ShouldCollideLocked(const JPH::Body& inBody) const override
        {
            return _bodyID != inBody.GetID();
        }

    private:
        JPH::BodyID _bodyID;
    };

    void UpdateUnitEntities::Init(entt::registry& registry)
    {
    }

    void UpdateUnitEntities::Update(entt::registry& registry, f32 deltaTime)
    {
        ZoneScopedN("ECS::UpdateUnitEntities");

        TransformSystem& transformSystem = TransformSystem::Get(registry);
        auto& characterSingleton = registry.ctx().get<Singletons::CharacterSingleton>();

        entt::registry* dbRegistry = ServiceLocator::GetEnttRegistries()->dbRegistry;
        auto& clientDBSingleton = dbRegistry->ctx().get<Singletons::ClientDBSingleton>();
        auto& unitCustomizationSingleton = dbRegistry->ctx().get<Singletons::UnitCustomizationSingleton>();
        auto& textureSingleton = dbRegistry->ctx().get<Singletons::TextureSingleton>();

        GameRenderer* gameRenderer = ServiceLocator::GetGameRenderer();
        ModelLoader* modelLoader = gameRenderer->GetModelLoader();

        auto modelLoadedEventView = registry.view<Components::ModelLoadedEvent>();
        if (modelLoadedEventView.size() > 0)
        {
            auto* unitRaceStorage = clientDBSingleton.Get(ClientDBHash::UnitRace);
            auto* creatureDisplayInfoStorage = clientDBSingleton.Get(ClientDBHash::CreatureDisplayInfo);
            auto* creatureDisplayInfoExtraStorage = clientDBSingleton.Get(ClientDBHash::CreatureDisplayInfoExtra);
            auto* unitTextureSectionStorage = clientDBSingleton.Get(ClientDBHash::UnitTextureSection);
            auto* unitCustomizationOptionStorage = clientDBSingleton.Get(ClientDBHash::UnitCustomizationOption);
            auto* unitCustomizationGeosetStorage = clientDBSingleton.Get(ClientDBHash::UnitCustomizationGeoset);
            auto* unitCustomizationMaterialStorage = clientDBSingleton.Get(ClientDBHash::UnitCustomizationMaterial);
            auto* unitRaceCustomizationChoiceStorage = clientDBSingleton.Get(ClientDBHash::UnitRaceCustomizationChoice);

            modelLoadedEventView.each([&](entt::entity entity, Components::ModelLoadedEvent& modelLoadedEvent)
            {
                if (modelLoadedEvent.flags.staticModel)
                    return;

                auto& model = registry.get<Components::Model>(entity);
                auto* name = registry.try_get<Components::Name>(entity);

                if (modelLoadedEvent.flags.loaded)
                {
                    if (model.modelHash == std::numeric_limits<u64>().max())
                    {
                        NC_LOG_INFO("Entity \"{0}\" Loaded Model with Invalid ModelHash", name ? name->name : "Unknown");
                        return;
                    }

                    auto& discoveredModel = modelLoader->GetDiscoveredModel(model.modelHash);
                    modelLoader->DisableAllGroupsForEntity(entity);

                    if (auto* unitCustomization = registry.try_get<Components::UnitCustomization>(entity))
                    {
                        unitCustomization->flags = { 0 };
                        unitCustomization->componentSectionsInUse = { 0 };

                        auto& displayInfo = registry.get<Components::DisplayInfo>(entity);
                        if (auto* displayInfoRow = creatureDisplayInfoStorage->TryGet<MetaGen::Shared::ClientDB::CreatureDisplayInfoRecord>(displayInfo.displayID))
                        {
                            if (auto* displayInfoExtraRow = creatureDisplayInfoExtraStorage->TryGet<MetaGen::Shared::ClientDB::CreatureDisplayInfoExtraRecord>(displayInfoRow->extendedDisplayInfoID))
                            {
                                u64 bakedTextureHash = creatureDisplayInfoExtraStorage->GetStringHash(displayInfoExtraRow->bakedTexture);
                                unitCustomization->flags.useCustomSkin = bakedTextureHash == 0;
                                displayInfo.race = static_cast<GameDefine::UnitRace>(displayInfoExtraRow->raceID);
                                displayInfo.gender = static_cast<GameDefine::UnitGender>(displayInfoExtraRow->gender);

                                unitCustomization->skinID = displayInfoExtraRow->skinID;
                                unitCustomization->faceID = displayInfoExtraRow->faceID;
                                unitCustomization->facialHairID = displayInfoExtraRow->facialHairID;
                                unitCustomization->hairStyleID = displayInfoExtraRow->hairStyleID;
                                unitCustomization->hairColorID = displayInfoExtraRow->hairColorID;
                            }
                            else
                            {
                                if (unitCustomizationSingleton.modelIDToUnitModelInfo.contains(displayInfoRow->modelID))
                                {
                                    auto& unitModelInfo = unitCustomizationSingleton.modelIDToUnitModelInfo[displayInfoRow->modelID];
                                    displayInfo.race = unitModelInfo.race;
                                    displayInfo.gender = unitModelInfo.gender;

                                    unitCustomization->flags.useCustomSkin = true;
                                }
                            }
                        }

                        unitCustomization->flags.forceRefresh = true;
                        registry.emplace_or_replace<ECS::Components::UnitRebuildSkinTexture>(entity);
                    }

                    if (auto* modelQueuedGeometryGroups = registry.try_get<Components::ModelQueuedGeometryGroups>(entity))
                    {
                        for (u32 groupID : modelQueuedGeometryGroups->enabledGroupIDs)
                        {
                            modelLoader->EnableGroupForEntity(entity, groupID);
                        }

                        registry.erase<Components::ModelQueuedGeometryGroups>(entity);
                    }
                    else
                    {
                        registry.emplace_or_replace<ECS::Components::UnitRebuildGeosets>(entity);
                    }

                    if (auto* unitEquipment = registry.try_get<Components::UnitEquipment>(entity))
                    {
                        bool isEquipmentDirty = false;
                        bool isVisualEquipmentDirty = false;
                        for (u32 i = (u32)MetaGen::Shared::Unit::ItemEquipSlotEnum::EquipmentStart; i <= (u32)MetaGen::Shared::Unit::ItemEquipSlotEnum::EquipmentEnd; i++)
                        {
                            u32 equipSlotIndex = i;

                            u32 equippedItemID = unitEquipment->equipmentSlotToItemID[equipSlotIndex];
                            if (equippedItemID != 0)
                            {
                                unitEquipment->dirtyItemIDSlots.insert((MetaGen::Shared::Unit::ItemEquipSlotEnum)equipSlotIndex);
                                isEquipmentDirty = true;
                            }

                            u32 visualItemID = unitEquipment->equipmentSlotToVisualItemID[equipSlotIndex];
                            if (visualItemID != 0)
                            {
                                unitEquipment->dirtyVisualItemIDSlots.insert((MetaGen::Shared::Unit::ItemEquipSlotEnum)equipSlotIndex);
                                isVisualEquipmentDirty = true;
                            }
                        }

                        if (isEquipmentDirty)
                            registry.get_or_emplace<Components::UnitEquipmentDirty>(entity);

                        if (isVisualEquipmentDirty)
                            registry.get_or_emplace<Components::UnitVisualEquipmentDirty>(entity);
                    }

                }
                else
                {
                    if (registry.all_of<Components::ModelQueuedGeometryGroups>(entity))
                        registry.erase<Components::ModelQueuedGeometryGroups>(entity);
                }
            });

            registry.clear<Components::ModelLoadedEvent>();
        }

        Util::RemoteUnitPresentation::Update(registry, deltaTime);

        auto* itemStorage = clientDBSingleton.Get(ClientDBHash::Item);

        auto unitVisualEquipmentView = registry.view<const Components::Unit, const Components::DisplayInfo, const Components::Model, const Components::UnitCustomization, Components::UnitEquipment, Components::UnitVisualEquipmentDirty>();
        unitVisualEquipmentView.each([&](entt::entity entity, const Components::Unit& unit, const Components::DisplayInfo& displayInfo, const Components::Model& model, const Components::UnitCustomization& unitCustomization, Components::UnitEquipment& unitEquipment)
        {
            if (!model.flags.loaded)
                return;

            bool needToRefreshGeometry = false;
            bool needToRefreshSkin = false;

            for (const MetaGen::Shared::Unit::ItemEquipSlotEnum equipSlot : unitEquipment.dirtyVisualItemIDSlots)
            {
                needToRefreshGeometry |= equipSlot != ::MetaGen::Shared::Unit::ItemEquipSlotEnum::MainHand && equipSlot != ::MetaGen::Shared::Unit::ItemEquipSlotEnum::OffHand && equipSlot != ::MetaGen::Shared::Unit::ItemEquipSlotEnum::Ranged;
                needToRefreshSkin |= equipSlot == ::MetaGen::Shared::Unit::ItemEquipSlotEnum::Chest || equipSlot == ::MetaGen::Shared::Unit::ItemEquipSlotEnum::Shirt || equipSlot == ::MetaGen::Shared::Unit::ItemEquipSlotEnum::Tabard ||
                    equipSlot == ::MetaGen::Shared::Unit::ItemEquipSlotEnum::Bracers || equipSlot == ::MetaGen::Shared::Unit::ItemEquipSlotEnum::Gloves || equipSlot == ::MetaGen::Shared::Unit::ItemEquipSlotEnum::Belt ||
                    equipSlot == ::MetaGen::Shared::Unit::ItemEquipSlotEnum::Pants || equipSlot == ::MetaGen::Shared::Unit::ItemEquipSlotEnum::Boots;
            }

            if (unitCustomization.flags.useCustomSkin)
            {
                if (needToRefreshGeometry)
                    registry.emplace_or_replace<ECS::Components::UnitRebuildGeosets>(entity);

                if (needToRefreshSkin)
                    registry.emplace_or_replace<ECS::Components::UnitRebuildSkinTexture>(entity);
            }

            unitEquipment.dirtyVisualItemIDSlots.clear();
        });

        registry.clear<Components::UnitVisualEquipmentDirty>();

        // TODO : Handle Equipped Item Changes (Client Side Calculation of stats?)
        registry.clear<Components::UnitEquipmentDirty>();

        auto unitRebuildGeosetsView = registry.view<const Components::Unit, const Components::Model, ECS::Components::UnitRebuildGeosets>();
        unitRebuildGeosetsView.each([&](entt::entity entity, const Components::Unit& unit, const Components::Model& model)
        {
            if (!model.flags.loaded)
                return;

            ::Util::Unit::DisableAllGeometryGroups(registry, entity, model);
            ::Util::Unit::RefreshGeometryGroups(registry, entity, clientDBSingleton, unitCustomizationSingleton, model);
        });

        registry.clear<Components::UnitRebuildGeosets>();

        TextureRenderer* textureRenderer = ServiceLocator::GetGameRenderer()->GetTextureRenderer();
        auto& itemSingleton = dbRegistry->ctx().get<Singletons::ItemSingleton>();

        auto UnitRebuildSkinTextureView = registry.view<const Components::Unit, Components::UnitCustomization, const Components::Model, Components::DisplayInfo, ECS::Components::UnitRebuildSkinTexture>();
        UnitRebuildSkinTextureView.each([&](entt::entity entity, const Components::Unit& unit, Components::UnitCustomization& unitCustomization, const Components::Model& model, Components::DisplayInfo& displayInfo)
        {
            if (!model.flags.loaded)
                return;

            ::Util::Unit::RefreshSkinTexture(*dbRegistry, entity, clientDBSingleton, unitCustomizationSingleton, displayInfo, unitCustomization, model);

            if (auto* unitEquipment = registry.try_get<Components::UnitEquipment>(entity))
            {
                if (!unitCustomization.flags.hasGloveModel)
                {
                    u32 glovesID = unitEquipment->equipmentSlotToVisualItemID[(u32)MetaGen::Shared::Unit::ItemEquipSlotEnum::Gloves];
                    if (glovesID > 0)
                    {
                        if (itemStorage->Has(glovesID))
                        {
                            auto& item = itemStorage->Get<MetaGen::Shared::ClientDB::ItemRecord>(glovesID);
                            if (item.displayID > 0)
                            {
                                ECSUtil::UnitCustomization::WriteItemToSkin(textureSingleton, clientDBSingleton, itemSingleton, unitCustomizationSingleton, unitCustomization, item.displayID);
                            }
                        }
                    }
                }

                u32 shirtID = unitEquipment->equipmentSlotToVisualItemID[(u32)MetaGen::Shared::Unit::ItemEquipSlotEnum::Shirt];
                if (shirtID > 0)
                {
                    if (itemStorage->Has(shirtID))
                    {
                        auto& item = itemStorage->Get<MetaGen::Shared::ClientDB::ItemRecord>(shirtID);
                        if (item.displayID > 0)
                        {
                            ECSUtil::UnitCustomization::WriteItemToSkin(textureSingleton, clientDBSingleton, itemSingleton, unitCustomizationSingleton, unitCustomization, item.displayID);
                        }
                    }
                }

                u32 bracersID = unitEquipment->equipmentSlotToVisualItemID[(u32)MetaGen::Shared::Unit::ItemEquipSlotEnum::Bracers];
                if (bracersID > 0)
                {
                    if (itemStorage->Has(bracersID))
                    {
                        auto& item = itemStorage->Get<MetaGen::Shared::ClientDB::ItemRecord>(bracersID);
                        if (item.displayID > 0)
                        {
                            ECSUtil::UnitCustomization::WriteItemToSkin(textureSingleton, clientDBSingleton, itemSingleton, unitCustomizationSingleton, unitCustomization, item.displayID);
                        }
                    }
                }

                u32 bootsID = unitEquipment->equipmentSlotToVisualItemID[(u32)MetaGen::Shared::Unit::ItemEquipSlotEnum::Boots];
                if (bootsID > 0)
                {
                    if (itemStorage->Has(bootsID))
                    {
                        auto& item = itemStorage->Get<MetaGen::Shared::ClientDB::ItemRecord>(bootsID);
                        if (item.displayID > 0)
                        {
                            ECSUtil::UnitCustomization::WriteItemToSkin(textureSingleton, clientDBSingleton, itemSingleton, unitCustomizationSingleton, unitCustomization, item.displayID);
                        }
                    }
                }

                u32 pantsID = unitEquipment->equipmentSlotToVisualItemID[(u32)MetaGen::Shared::Unit::ItemEquipSlotEnum::Pants];
                if (pantsID > 0)
                {
                    if (itemStorage->Has(pantsID))
                    {
                        auto& item = itemStorage->Get<MetaGen::Shared::ClientDB::ItemRecord>(pantsID);
                        if (item.displayID > 0)
                        {
                            ECSUtil::UnitCustomization::WriteItemToSkin(textureSingleton, clientDBSingleton, itemSingleton, unitCustomizationSingleton, unitCustomization, item.displayID);
                        }
                    }
                }

                u32 chestID = unitEquipment->equipmentSlotToVisualItemID[(u32)MetaGen::Shared::Unit::ItemEquipSlotEnum::Chest];
                if (chestID > 0)
                {
                    if (itemStorage->Has(chestID))
                    {
                        auto& item = itemStorage->Get<MetaGen::Shared::ClientDB::ItemRecord>(chestID);
                        if (item.displayID > 0)
                        {
                            ECSUtil::UnitCustomization::WriteItemToSkin(textureSingleton, clientDBSingleton, itemSingleton, unitCustomizationSingleton, unitCustomization, item.displayID);
                        }
                    }
                }

                if (unitCustomization.flags.hasGloveModel)
                {
                    u32 glovesID = unitEquipment->equipmentSlotToVisualItemID[(u32)MetaGen::Shared::Unit::ItemEquipSlotEnum::Gloves];
                    if (glovesID > 0)
                    {
                        if (itemStorage->Has(glovesID))
                        {
                            auto& item = itemStorage->Get<MetaGen::Shared::ClientDB::ItemRecord>(glovesID);
                            if (item.displayID > 0)
                            {
                                ECSUtil::UnitCustomization::WriteItemToSkin(textureSingleton, clientDBSingleton, itemSingleton, unitCustomizationSingleton, unitCustomization, item.displayID);
                            }
                        }
                    }
                }

                u32 beltID = unitEquipment->equipmentSlotToVisualItemID[(u32)MetaGen::Shared::Unit::ItemEquipSlotEnum::Belt];
                if (beltID > 0)
                {
                    if (itemStorage->Has(beltID))
                    {
                        auto& item = itemStorage->Get<MetaGen::Shared::ClientDB::ItemRecord>(beltID);
                        if (item.displayID > 0)
                        {
                            ECSUtil::UnitCustomization::WriteItemToSkin(textureSingleton, clientDBSingleton, itemSingleton, unitCustomizationSingleton, unitCustomization, item.displayID);
                        }
                    }
                }

                u32 tabardID = unitEquipment->equipmentSlotToVisualItemID[(u32)MetaGen::Shared::Unit::ItemEquipSlotEnum::Tabard];
                if (tabardID > 0)
                {
                    if (itemStorage->Has(tabardID))
                    {
                        auto& item = itemStorage->Get<MetaGen::Shared::ClientDB::ItemRecord>(tabardID);
                        if (item.displayID > 0)
                        {
                            ECSUtil::UnitCustomization::WriteItemToSkin(textureSingleton, clientDBSingleton, itemSingleton, unitCustomizationSingleton, unitCustomization, item.displayID);
                        }
                    }
                }
            }
        });

        registry.clear<Components::UnitRebuildSkinTexture>();
    }
}
