#include "UpdateUnitEntities.h"

#include "Game-Lib/ECS/Components/AnimationData.h"
#include "Game-Lib/ECS/Components/DisplayInfo.h"
#include "Game-Lib/ECS/Components/Events.h"
#include "Game-Lib/ECS/Components/Model.h"
#include "Game-Lib/ECS/Components/MovementInfo.h"
#include "Game-Lib/ECS/Components/Name.h"
#include "Game-Lib/ECS/Components/Tags.h"
#include "Game-Lib/ECS/Components/Unit.h"
#include "Game-Lib/ECS/Components/UnitCustomization.h"
#include "Game-Lib/ECS/Components/UnitEquipment.h"
#include "Game-Lib/ECS/Components/UnitMovementOverTime.h"
#include "Game-Lib/ECS/Components/UnitStatsComponent.h"
#include "Game-Lib/ECS/Singletons/CharacterSingleton.h"
#include "Game-Lib/ECS/Singletons/JoltState.h"
#include "Game-Lib/ECS/Singletons/Database/ClientDBSingleton.h"
#include "Game-Lib/ECS/Singletons/Database/UnitCustomizationSingleton.h"
#include "Game-Lib/ECS/Singletons/Database/TextureSingleton.h"
#include "Game-Lib/ECS/Util/Transforms.h"
#include "Game-Lib/ECS/Util/Database/TextureUtil.h"
#include "Game-Lib/ECS/Util/Database/UnitCustomizationUtil.h"
#include "Game-Lib/Gameplay/Animation/Defines.h"
#include "Game-Lib/Gameplay/Database/Unit.h"
#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Rendering/Model/ModelLoader.h"
#include "Game-Lib/Rendering/Texture/TextureRenderer.h"
#include "Game-Lib/Util/AnimationUtil.h"
#include "Game-Lib/Util/ServiceLocator.h"
#include "Game-Lib/Util/UnitUtil.h"

#include <Meta/Generated/ClientDB.h>

#include <entt/entt.hpp>
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/RayCast.h>

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

    void SetOrientation(vec4& settings, f32 orientation)
    {
        f32 currentOrientation = settings.x;
        if (orientation == currentOrientation)
            return;

        settings.y = orientation;
        settings.w = 0.0f;

        f32 timeToChange = 0.15f;
        settings.z = timeToChange;
    };

    bool HandleUpdateOrientation(vec4& settings, f32 deltaTime)
    {
        if (settings.x == settings.y)
            return false;

        settings.w += deltaTime;
        settings.w = glm::clamp(settings.w, 0.0f, settings.z);

        f32 progress = settings.w / settings.z;
        settings.x = glm::mix(settings.x, settings.y, progress);

        return true;
    };

    void UpdateUnitEntities::Update(entt::registry& registry, f32 deltaTime)
    {
        ZoneScopedN("ECS::UpdateUnitEntities");

        Singletons::JoltState& joltState = registry.ctx().get<Singletons::JoltState>();
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
                auto& model = registry.get<Components::Model>(entity);
                auto* name = registry.try_get<Components::Name>(entity);

                if (modelLoadedEvent.flags.loaded)
                {
                    auto& discoveredModel = modelLoader->GetDiscoveredModel(model.modelHash);
                    NC_LOG_INFO("Entity \"{0}\" Loaded New Model \"{1}\"", name->name, discoveredModel.name);

                    modelLoader->DisableAllGroupsForModel(model);

                    if (auto* unitCustomization = registry.try_get<Components::UnitCustomization>(entity))
                    {
                        unitCustomization->flags = { 0 };
                        unitCustomization->componentSectionsInUse = { 0 };

                        auto& displayInfo = registry.get<Components::DisplayInfo>(entity);
                        if (auto* displayInfoRow = creatureDisplayInfoStorage->TryGet<Generated::CreatureDisplayInfoRecord>(displayInfo.displayID))
                        {
                            if (auto* displayInfoExtraRow = creatureDisplayInfoExtraStorage->TryGet<Generated::CreatureDisplayInfoExtraRecord>(displayInfoRow->extendedDisplayInfoID))
                            {
                                u32 bakedTextureHash = creatureDisplayInfoExtraStorage->GetStringHash(displayInfoExtraRow->bakedTexture);
                                unitCustomization->flags.useCustomSkin = !textureSingleton.textureHashToPath.contains(bakedTextureHash);

                                displayInfo.race = static_cast<GameDefine::UnitRace>(displayInfoExtraRow->raceID);
                                displayInfo.gender = static_cast<GameDefine::Gender>(displayInfoExtraRow->gender);

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

                        if (unitCustomization->flags.useCustomSkin)
                        {
                            unitCustomization->flags.forceRefresh = true;
                            registry.emplace_or_replace<ECS::Components::UnitRebuildSkinTexture>(entity);
                        }
                    }

                    if (auto* modelQueuedGeometryGroups = registry.try_get<Components::ModelQueuedGeometryGroups>(entity))
                    {
                        for (u32 groupID : modelQueuedGeometryGroups->enabledGroupIDs)
                        {
                            modelLoader->EnableGroupForModel(model, groupID);
                        }

                        registry.erase<Components::ModelQueuedGeometryGroups>(entity);
                    }
                    else
                    {
                        registry.emplace_or_replace<ECS::Components::UnitRebuildGeosets>(entity);
                    }

                    if (auto* unitEquipment = registry.try_get<Components::UnitEquipment>(entity))
                    {
                        bool isDirty = false;
                        for (u32 i = (u32)Database::Item::ItemEquipSlot::Helm; i < (u32)Database::Item::ItemEquipSlot::Count; i++)
                        {
                            u32 equipSlotIndex = i;

                            u32 itemID = unitEquipment->equipmentSlotToItemID[equipSlotIndex];
                            if (itemID == 0)
                                continue;

                            unitEquipment->dirtyEquipmentSlots.insert((Database::Item::ItemEquipSlot)equipSlotIndex);
                            isDirty = true;
                        }

                        if (isDirty)
                        {
                            registry.get_or_emplace<Components::UnitEquipmentDirty>(entity);
                        }
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

        auto unitMovementOverTimeView = registry.view<Components::Transform, Components::Unit, Components::UnitMovementOverTime>();
        unitMovementOverTimeView.each([&](entt::entity entity, Components::Transform& transform, Components::Unit& unit, Components::UnitMovementOverTime& unitMovementOverTime)
        {
            if (unitMovementOverTime.time == 1.0f)
               return;

            unitMovementOverTime.time += 10.0f * deltaTime;
            unitMovementOverTime.time = glm::min(unitMovementOverTime.time, 1.0f);

            f32 progress = unitMovementOverTime.time;
            vec3 startPos = unitMovementOverTime.startPos;
            vec3 endPos = unitMovementOverTime.endPos;
            vec3 newPosition = glm::mix(startPos, endPos, progress);

            transformSystem.SetWorldPosition(entity, newPosition);

            if (unit.bodyID == std::numeric_limits<u32>().max())
                return;

            JPH::BodyID bodyID = JPH::BodyID(unit.bodyID);
            quat rotation = transform.GetWorldRotation();

            joltState.physicsSystem.GetBodyInterface().SetPositionAndRotation(bodyID, JPH::Vec3(newPosition.x, newPosition.y, newPosition.z), JPH::Quat(rotation.x, rotation.y, rotation.z, rotation.w), JPH::EActivation::DontActivate);
        });

        auto unitView = registry.view<Components::MovementInfo, Components::Unit, Components::Model>();
        unitView.each([&](entt::entity entity, Components::MovementInfo& movementInfo, Components::Unit& unit, Components::Model& model)
        {
            if (!registry.all_of<Components::AnimationData>(entity))
                return;

            if (unit.overrideAnimation == ::Animation::Defines::Type::Invalid)
                ::Util::Unit::UpdateAnimationState(registry, entity, model, deltaTime);

            auto& unitStatsComponent = registry.get<Components::UnitStatsComponent>(entity);
            bool isAlive = unitStatsComponent.currentHealth > 0.0f;
            if (!isAlive)
                return;

            bool isMovingForward = movementInfo.movementFlags.forward;
            bool isMovingBackward = movementInfo.movementFlags.backward;
            bool isMovingLeft = movementInfo.movementFlags.left;
            bool isMovingRight = movementInfo.movementFlags.right;
            bool isGrounded = movementInfo.movementFlags.grounded;
            bool isFlying = !isGrounded && movementInfo.movementFlags.flying;

            const auto* modelInfo = modelLoader->GetModelInfo(model.modelHash);
            if (isAlive && modelInfo && (isGrounded || isFlying /* || (canControlInAir && isMoving))*/))
            {
                f32 spineOrientation = 0.0f;
                f32 headOrientation = 0.0f;
                f32 waistOrientation = 0.0f;

                if (!isFlying)
                {
                    if (isMovingForward)
                        {
                            if (isMovingRight)
                            {
                                spineOrientation = -30.0f;
                                headOrientation = -30.0f;
                                waistOrientation = 45.0f;
                            }
                            else if (isMovingLeft)
                            {
                                spineOrientation = 30.0f;
                                headOrientation = 30.0f;
                                waistOrientation = -45.0f;
                            }
                        }
                    else if (isMovingBackward)
                        {
                            if (isMovingRight)
                            {
                                spineOrientation = 30.0f;
                                headOrientation = 15.0f;
                                waistOrientation = -45.0f;
                            }
                            else if (isMovingLeft)
                            {
                                spineOrientation = -30.0f;
                                headOrientation = -15.0f;
                                waistOrientation = 45.0f;
                            }
                        }
                    else if (isMovingRight)
                        {
                            spineOrientation = -45.0f;
                            headOrientation = -30.0f;
                            waistOrientation = 90.0f;
                        }
                    else if (isMovingLeft)
                        {
                            spineOrientation = 45.0f;
                            headOrientation = 30.0f;
                            waistOrientation = -90.0f;
                        }
                }

                SetOrientation(movementInfo.spineRotationSettings, spineOrientation);
                SetOrientation(movementInfo.headRotationSettings, headOrientation);
                SetOrientation(movementInfo.rootRotationSettings, waistOrientation);
            }

            if (HandleUpdateOrientation(movementInfo.spineRotationSettings, deltaTime))
            {
                auto& animationData = registry.get<Components::AnimationData>(entity);

                quat rotation = glm::quat(glm::vec3(0.0f, glm::radians(movementInfo.spineRotationSettings.x), 0.0f));
                ::Util::Animation::SetBoneRotation(modelInfo, animationData, ::Animation::Defines::Bone::SpineLow, rotation);
            }
            if (HandleUpdateOrientation(movementInfo.headRotationSettings, deltaTime))
            {
                auto& animationData = registry.get<Components::AnimationData>(entity);

                quat rotation = glm::quat(glm::vec3(0.0f, glm::radians(movementInfo.headRotationSettings.x), 0.0f));
                ::Util::Animation::SetBoneRotation(modelInfo, animationData, ::Animation::Defines::Bone::Head, rotation);
            }
            if (HandleUpdateOrientation(movementInfo.rootRotationSettings, deltaTime))
            {
                auto& animationData = registry.get<Components::AnimationData>(entity);

                quat rotation = glm::quat(glm::vec3(0.0f, glm::radians(movementInfo.rootRotationSettings.x), 0.0f));
                ::Util::Animation::SetBoneRotation(modelInfo, animationData, ::Animation::Defines::Bone::Default, rotation);
            }
        });

        auto* itemStorage = clientDBSingleton.Get(ClientDBHash::Item);

        auto unitEquipmentView = registry.view<const Components::Unit, const Components::DisplayInfo, const Components::Model, const Components::UnitCustomization, Components::UnitEquipment, Components::UnitEquipmentDirty>();
        unitEquipmentView.each([&](entt::entity entity, const Components::Unit& unit, const Components::DisplayInfo& displayInfo, const Components::Model& model, const Components::UnitCustomization& unitCustomization, Components::UnitEquipment& unitEquipment)
        {
            if (!model.flags.loaded)
                return;

            bool needToRefreshGeometry = false;
            bool needToRefreshSkin = false;

            for (const Database::Item::ItemEquipSlot equipSlot : unitEquipment.dirtyEquipmentSlots)
            {
                u32 itemID = unitEquipment.equipmentSlotToItemID[(u32)equipSlot];

                needToRefreshGeometry |= equipSlot != ::Database::Item::ItemEquipSlot::MainHand && equipSlot != ::Database::Item::ItemEquipSlot::OffHand && equipSlot != ::Database::Item::ItemEquipSlot::Ranged;
                needToRefreshSkin |= equipSlot == ::Database::Item::ItemEquipSlot::Chest || equipSlot == ::Database::Item::ItemEquipSlot::Shirt || equipSlot == ::Database::Item::ItemEquipSlot::Tabard || 
                    equipSlot == ::Database::Item::ItemEquipSlot::Bracers || equipSlot == ::Database::Item::ItemEquipSlot::Gloves || equipSlot == ::Database::Item::ItemEquipSlot::Belt || 
                    equipSlot == ::Database::Item::ItemEquipSlot::Pants || equipSlot == ::Database::Item::ItemEquipSlot::Boots;

                if (itemID == 0)
                {
                    if (equipSlot == ::Database::Item::ItemEquipSlot::MainHand)
                    {
                        ::Util::Unit::OpenHand(registry, entity, false);
                        ::Util::Unit::RemoveItemFromAttachment(registry, entity, ::Attachment::Defines::Type::HandRight);
                    }
                    else if (equipSlot == ::Database::Item::ItemEquipSlot::OffHand)
                    {
                        ::Util::Unit::OpenHand(registry, entity, true);
                        ::Util::Unit::RemoveItemFromAttachment(registry, entity, ::Attachment::Defines::Type::HandLeft);
                        ::Util::Unit::RemoveItemFromAttachment(registry, entity, ::Attachment::Defines::Type::Shield);
                    }
                    else if (equipSlot == ::Database::Item::ItemEquipSlot::Helm)
                    {
                        ::Util::Unit::RemoveItemFromAttachment(registry, entity, ::Attachment::Defines::Type::Helm);
                    }
                    else if (equipSlot == ::Database::Item::ItemEquipSlot::Shoulders)
                    {
                        ::Util::Unit::RemoveItemFromAttachment(registry, entity, ::Attachment::Defines::Type::ShoulderLeft);
                        ::Util::Unit::RemoveItemFromAttachment(registry, entity, ::Attachment::Defines::Type::ShoulderRight);
                    }
                }
                else
                {
                    auto& item = itemStorage->Get<Generated::ItemRecord>(itemID);

                    if (equipSlot == ::Database::Item::ItemEquipSlot::MainHand)
                    {
                        entt::entity itemEntity = entt::null;

                        if (::Util::Unit::AddWeaponToHand(registry, entity, item, false, itemEntity))
                            ::Util::Unit::CloseHand(registry, entity, false);
                    }
                    else if (equipSlot == ::Database::Item::ItemEquipSlot::OffHand)
                    {
                        entt::entity itemEntity = entt::null;

                        if (::Util::Unit::AddWeaponToHand(registry, entity, item, true, itemEntity))
                            ::Util::Unit::CloseHand(registry, entity, true);
                    }
                    else if (equipSlot == ::Database::Item::ItemEquipSlot::Helm)
                    {
                        entt::entity itemEntity = entt::null;

                        ::Util::Unit::AddHelm(registry, entity, item, displayInfo.race, displayInfo.gender, itemEntity);
                    }
                    else if (equipSlot == ::Database::Item::ItemEquipSlot::Shoulders)
                    {
                        entt::entity shoulderLeftEntity = entt::null;
                        entt::entity shoulderRightEntity = entt::null;

                        ::Util::Unit::AddShoulders(registry, entity, item, shoulderLeftEntity, shoulderRightEntity);
                    }
                }
            }

            if (unitCustomization.flags.useCustomSkin)
            {
                if (needToRefreshGeometry)
                    registry.emplace_or_replace<ECS::Components::UnitRebuildGeosets>(entity);

                if (needToRefreshSkin)
                    registry.emplace_or_replace<ECS::Components::UnitRebuildSkinTexture>(entity);
            }

            unitEquipment.dirtyEquipmentSlots.clear();
        });
        
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
                    u32 glovesID = unitEquipment->equipmentSlotToItemID[(u32)Database::Item::ItemEquipSlot::Gloves];
                    if (glovesID > 0)
                    {
                        if (itemStorage->Has(glovesID))
                        {
                            auto& item = itemStorage->Get<Generated::ItemRecord>(glovesID);
                            if (item.displayID > 0)
                            {
                                ECSUtil::UnitCustomization::WriteItemToSkin(textureSingleton, clientDBSingleton, itemSingleton, unitCustomizationSingleton, unitCustomization, item.displayID);
                            }
                        }
                    }
                }

                u32 shirtID = unitEquipment->equipmentSlotToItemID[(u32)Database::Item::ItemEquipSlot::Shirt];
                if (shirtID > 0)
                {
                    if (itemStorage->Has(shirtID))
                    {
                        auto& item = itemStorage->Get<Generated::ItemRecord>(shirtID);
                        if (item.displayID > 0)
                        {
                            ECSUtil::UnitCustomization::WriteItemToSkin(textureSingleton, clientDBSingleton, itemSingleton, unitCustomizationSingleton, unitCustomization, item.displayID);
                        }
                    }
                }

                u32 bracersID = unitEquipment->equipmentSlotToItemID[(u32)Database::Item::ItemEquipSlot::Bracers];
                if (bracersID > 0)
                {
                    if (itemStorage->Has(bracersID))
                    {
                        auto& item = itemStorage->Get<Generated::ItemRecord>(bracersID);
                        if (item.displayID > 0)
                        {
                            ECSUtil::UnitCustomization::WriteItemToSkin(textureSingleton, clientDBSingleton, itemSingleton, unitCustomizationSingleton, unitCustomization, item.displayID);
                        }
                    }
                }

                u32 bootsID = unitEquipment->equipmentSlotToItemID[(u32)Database::Item::ItemEquipSlot::Boots];
                if (bootsID > 0)
                {
                    if (itemStorage->Has(bootsID))
                    {
                        auto& item = itemStorage->Get<Generated::ItemRecord>(bootsID);
                        if (item.displayID > 0)
                        {
                            ECSUtil::UnitCustomization::WriteItemToSkin(textureSingleton, clientDBSingleton, itemSingleton, unitCustomizationSingleton, unitCustomization, item.displayID);
                        }
                    }
                }

                u32 pantsID = unitEquipment->equipmentSlotToItemID[(u32)Database::Item::ItemEquipSlot::Pants];
                if (pantsID > 0)
                {
                    if (itemStorage->Has(pantsID))
                    {
                        auto& item = itemStorage->Get<Generated::ItemRecord>(pantsID);
                        if (item.displayID > 0)
                        {
                            ECSUtil::UnitCustomization::WriteItemToSkin(textureSingleton, clientDBSingleton, itemSingleton, unitCustomizationSingleton, unitCustomization, item.displayID);
                        }
                    }
                }

                u32 chestID = unitEquipment->equipmentSlotToItemID[(u32)Database::Item::ItemEquipSlot::Chest];
                if (chestID > 0)
                {
                    if (itemStorage->Has(chestID))
                    {
                        auto& item = itemStorage->Get<Generated::ItemRecord>(chestID);
                        if (item.displayID > 0)
                        {
                            ECSUtil::UnitCustomization::WriteItemToSkin(textureSingleton, clientDBSingleton, itemSingleton, unitCustomizationSingleton, unitCustomization, item.displayID);
                        }
                    }
                }

                if (unitCustomization.flags.hasGloveModel)
                {
                    u32 glovesID = unitEquipment->equipmentSlotToItemID[(u32)Database::Item::ItemEquipSlot::Gloves];
                    if (glovesID > 0)
                    {
                        if (itemStorage->Has(glovesID))
                        {
                            auto& item = itemStorage->Get<Generated::ItemRecord>(glovesID);
                            if (item.displayID > 0)
                            {
                                ECSUtil::UnitCustomization::WriteItemToSkin(textureSingleton, clientDBSingleton, itemSingleton, unitCustomizationSingleton, unitCustomization, item.displayID);
                            }
                        }
                    }
                }

                u32 beltID = unitEquipment->equipmentSlotToItemID[(u32)Database::Item::ItemEquipSlot::Belt];
                if (beltID > 0)
                {
                    if (itemStorage->Has(beltID))
                    {
                        auto& item = itemStorage->Get<Generated::ItemRecord>(beltID);
                        if (item.displayID > 0)
                        {
                            ECSUtil::UnitCustomization::WriteItemToSkin(textureSingleton, clientDBSingleton, itemSingleton, unitCustomizationSingleton, unitCustomization, item.displayID);
                        }
                    }
                }

                u32 tabardID = unitEquipment->equipmentSlotToItemID[(u32)Database::Item::ItemEquipSlot::Tabard];
                if (tabardID > 0)
                {
                    if (itemStorage->Has(tabardID))
                    {
                        auto& item = itemStorage->Get<Generated::ItemRecord>(tabardID);
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