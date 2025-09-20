#include "UpdateUnitEntities.h"

#include "Game-Lib/ECS/Components/AnimationData.h"
#include "Game-Lib/ECS/Components/AttachmentData.h"
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
#include "Game-Lib/ECS/Components/UnitPowersComponent.h"
#include "Game-Lib/ECS/Components/UnitResistancesComponent.h"
#include "Game-Lib/ECS/Components/UnitStatsComponent.h"
#include "Game-Lib/ECS/Singletons/CharacterSingleton.h"
#include "Game-Lib/ECS/Singletons/JoltState.h"
#include "Game-Lib/ECS/Singletons/NetworkState.h"
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
#include "Game-Lib/Util/AttachmentUtil.h"
#include "Game-Lib/Util/ServiceLocator.h"
#include "Game-Lib/Util/UnitUtil.h"

#include <Meta/Generated/Shared/ClientDB.h>

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

    void SetOrientation(vec4& settings, f32 orientation, f32 timeToChange = 0.15f)
    {
        f32 currentOrientation = settings.x;
        if (orientation == currentOrientation)
            return;

        settings.y = orientation;
        settings.z = timeToChange;
        settings.w = 0.0f;
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

        TransformSystem& transformSystem = TransformSystem::Get(registry);
        auto& characterSingleton = registry.ctx().get<Singletons::CharacterSingleton>();
        auto& joltState = registry.ctx().get<Singletons::JoltState>();
        auto& networkState = registry.ctx().get<Singletons::NetworkState>();

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
                    if (model.modelHash == std::numeric_limits<u32>().max())
                    {
                        NC_LOG_INFO("Entity \"{0}\" Loaded Model with Invalid ModelHash", name ? name->name : "Unknown");
                        return;
                    }

                    auto& discoveredModel = modelLoader->GetDiscoveredModel(model.modelHash);
#ifdef NC_DEBUG
                    NC_LOG_INFO("Entity \"{0}\" Loaded New Model \"{1}\"", name->name, discoveredModel.name);
#endif
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
                        bool isEquipmentDirty = false;
                        bool isVisualEquipmentDirty = false;
                        for (u32 i = (u32)Generated::ItemEquipSlotEnum::EquipmentStart; i <= (u32)Generated::ItemEquipSlotEnum::EquipmentEnd; i++)
                        {
                            u32 equipSlotIndex = i;

                            u32 equippedItemID = unitEquipment->equipmentSlotToItemID[equipSlotIndex];
                            if (equippedItemID != 0)
                            {
                                unitEquipment->dirtyItemIDSlots.insert((Generated::ItemEquipSlotEnum)equipSlotIndex);
                                isEquipmentDirty = true;
                            }

                            u32 visualItemID = unitEquipment->equipmentSlotToVisualItemID[equipSlotIndex];
                            if (visualItemID != 0)
                            {
                                unitEquipment->dirtyVisualItemIDSlots.insert((Generated::ItemEquipSlotEnum)equipSlotIndex);
                                isVisualEquipmentDirty = true;
                            }
                        }

                        if (isEquipmentDirty)
                            registry.get_or_emplace<Components::UnitEquipmentDirty>(entity);

                        if (isVisualEquipmentDirty)
                            registry.get_or_emplace<Components::UnitVisualEquipmentDirty>(entity);
                    }

                    if (auto* unit = registry.try_get<Components::Unit>(entity))
                    {
                        auto& transform = registry.get<Components::Transform>(entity);

                        vec3 position = transform.GetWorldPosition();
                        vec3 scale = transform.GetLocalScale();

                        vec3 min = position - (scale * 0.5f);
                        vec3 max = position + (scale * 0.5f);
                        if (auto* aabb = registry.try_get<Components::AABB>(entity))
                        {
                            min = position + (aabb->centerPos - aabb->extents);
                            max = position + (aabb->centerPos + aabb->extents);
                        }

                        networkState.networkVisTree->Insert(&min.x, &max.x, unit->networkID);
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

            f32 scale = transform.GetLocalScale().x;
            vec3 min = newPosition - (scale * 0.5f);
            vec3 max = newPosition + (scale * 0.5f);
            if (auto* aabb = registry.try_get<Components::AABB>(entity))
            {
                min = newPosition + (aabb->centerPos - aabb->extents);
                max = newPosition + (aabb->centerPos + aabb->extents);
            }
            networkState.networkVisTree->Remove(unit.networkID);
            networkState.networkVisTree->Insert(&min.x, &max.x, unit.networkID);

            if (unit.bodyID == std::numeric_limits<u32>().max())
                return;

            JPH::BodyID bodyID = JPH::BodyID(unit.bodyID);
            quat rotation = transform.GetWorldRotation();

            joltState.physicsSystem.GetBodyInterface().SetPositionAndRotation(bodyID, JPH::Vec3(newPosition.x, newPosition.y, newPosition.z), JPH::Quat(rotation.x, rotation.y, rotation.z, rotation.w), JPH::EActivation::DontActivate);
        });

        auto unitView = registry.view<Components::Unit, Components::Model, Components::AnimationData, Components::MovementInfo>();
        unitView.each([&](entt::entity entity, Components::Unit& unit, Components::Model& model, Components::AnimationData& animationData, Components::MovementInfo& movementInfo)
        {
            if (!registry.all_of<>(entity))
                return;

            if (unit.overrideAnimation == ::Animation::Defines::Type::Invalid)
                ::Util::Unit::UpdateAnimationState(registry, entity, model, deltaTime);

            const auto* modelInfo = modelLoader->GetModelInfo(model.modelHash);

            if (auto* attachmentData = registry.try_get<Components::AttachmentData>(entity))
            {
                if (modelInfo)
                {
                    for (auto& pair : attachmentData->attachmentToInstance)
                    {
                        Util::Attachment::CalculateAttachmentMatrix(modelInfo, animationData, pair.first, pair.second, model.scale);
                    }
                }
            }

            auto& unitPowersComponent = registry.get<Components::UnitPowersComponent>(entity);
            auto& healthPower = ::Util::Unit::GetPower(unitPowersComponent, Generated::PowerTypeEnum::Health);

            bool isAlive = healthPower.current > 0.0f;
            if (!isAlive)
                return;

            bool isMovingForward = movementInfo.movementFlags.forward;
            bool isMovingBackward = movementInfo.movementFlags.backward;
            bool isMovingLeft = movementInfo.movementFlags.left;
            bool isMovingRight = movementInfo.movementFlags.right;
            bool isGrounded = movementInfo.movementFlags.grounded;
            bool isFlying = !isGrounded && movementInfo.movementFlags.flying;

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

                f32 timeToChange = 0.1f;
                if (!isMovingForward && !isMovingBackward && spineOrientation == 0.0f && headOrientation == 0.0f && waistOrientation == 0.0f)
                    timeToChange = 0.35f;

                SetOrientation(movementInfo.spineRotationSettings, spineOrientation, timeToChange);
                SetOrientation(movementInfo.headRotationSettings, headOrientation, timeToChange);
                SetOrientation(movementInfo.rootRotationSettings, waistOrientation, timeToChange);
            }

            if (HandleUpdateOrientation(movementInfo.spineRotationSettings, deltaTime))
            {
                quat rotation = glm::quat(glm::vec3(0.0f, glm::radians(movementInfo.spineRotationSettings.x), 0.0f));
                ::Util::Animation::SetBoneRotation(modelInfo, animationData, ::Animation::Defines::Bone::SpineLow, rotation);
            }
            if (HandleUpdateOrientation(movementInfo.headRotationSettings, deltaTime))
            {
                quat rotation = glm::quat(glm::vec3(0.0f, glm::radians(movementInfo.headRotationSettings.x), 0.0f));
                ::Util::Animation::SetBoneRotation(modelInfo, animationData, ::Animation::Defines::Bone::Head, rotation);
            }
            if (HandleUpdateOrientation(movementInfo.rootRotationSettings, deltaTime))
            {
                quat rotation = glm::quat(glm::vec3(0.0f, glm::radians(movementInfo.rootRotationSettings.x), 0.0f));
                ::Util::Animation::SetBoneRotation(modelInfo, animationData, ::Animation::Defines::Bone::Default, rotation);
            }
        });

        auto* itemStorage = clientDBSingleton.Get(ClientDBHash::Item);

        auto unitVisualEquipmentView = registry.view<const Components::Unit, const Components::DisplayInfo, const Components::Model, const Components::UnitCustomization, Components::UnitEquipment, Components::UnitVisualEquipmentDirty>();
        unitVisualEquipmentView.each([&](entt::entity entity, const Components::Unit& unit, const Components::DisplayInfo& displayInfo, const Components::Model& model, const Components::UnitCustomization& unitCustomization, Components::UnitEquipment& unitEquipment)
        {
            if (!model.flags.loaded)
                return;

            bool needToRefreshGeometry = false;
            bool needToRefreshSkin = false;

            for (const Generated::ItemEquipSlotEnum equipSlot : unitEquipment.dirtyVisualItemIDSlots)
            {
                u32 itemID = unitEquipment.equipmentSlotToVisualItemID[(u32)equipSlot];

                needToRefreshGeometry |= equipSlot != ::Generated::ItemEquipSlotEnum::MainHand && equipSlot != ::Generated::ItemEquipSlotEnum::OffHand && equipSlot != ::Generated::ItemEquipSlotEnum::Ranged;
                needToRefreshSkin |= equipSlot == ::Generated::ItemEquipSlotEnum::Chest || equipSlot == ::Generated::ItemEquipSlotEnum::Shirt || equipSlot == ::Generated::ItemEquipSlotEnum::Tabard || 
                    equipSlot == ::Generated::ItemEquipSlotEnum::Bracers || equipSlot == ::Generated::ItemEquipSlotEnum::Gloves || equipSlot == ::Generated::ItemEquipSlotEnum::Belt || 
                    equipSlot == ::Generated::ItemEquipSlotEnum::Pants || equipSlot == ::Generated::ItemEquipSlotEnum::Boots;

                if (itemID == 0)
                {
                    if (equipSlot == ::Generated::ItemEquipSlotEnum::MainHand)
                    {
                        ::Util::Unit::OpenHand(registry, entity, false);
                        ::Util::Unit::RemoveItemFromAttachment(registry, entity, ::Attachment::Defines::Type::HandRight);
                    }
                    else if (equipSlot == ::Generated::ItemEquipSlotEnum::OffHand)
                    {
                        ::Util::Unit::OpenHand(registry, entity, true);
                        ::Util::Unit::RemoveItemFromAttachment(registry, entity, ::Attachment::Defines::Type::HandLeft);
                        ::Util::Unit::RemoveItemFromAttachment(registry, entity, ::Attachment::Defines::Type::Shield);
                    }
                    else if (equipSlot == ::Generated::ItemEquipSlotEnum::Helm)
                    {
                        ::Util::Unit::RemoveItemFromAttachment(registry, entity, ::Attachment::Defines::Type::Helm);
                    }
                    else if (equipSlot == ::Generated::ItemEquipSlotEnum::Shoulders)
                    {
                        ::Util::Unit::RemoveItemFromAttachment(registry, entity, ::Attachment::Defines::Type::ShoulderLeft);
                        ::Util::Unit::RemoveItemFromAttachment(registry, entity, ::Attachment::Defines::Type::ShoulderRight);
                    }
                }
                else
                {
                    auto& item = itemStorage->Get<Generated::ItemRecord>(itemID);

                    if (equipSlot == ::Generated::ItemEquipSlotEnum::MainHand)
                    {
                        entt::entity itemEntity = entt::null;

                        if (::Util::Unit::AddWeaponToHand(registry, entity, item, false, itemEntity))
                            ::Util::Unit::CloseHand(registry, entity, false);
                    }
                    else if (equipSlot == ::Generated::ItemEquipSlotEnum::OffHand)
                    {
                        entt::entity itemEntity = entt::null;

                        if (::Util::Unit::AddWeaponToHand(registry, entity, item, true, itemEntity))
                            ::Util::Unit::CloseHand(registry, entity, true);
                    }
                    else if (equipSlot == ::Generated::ItemEquipSlotEnum::Helm)
                    {
                        entt::entity itemEntity = entt::null;

                        ::Util::Unit::AddHelm(registry, entity, item, displayInfo.race, displayInfo.gender, itemEntity);
                    }
                    else if (equipSlot == ::Generated::ItemEquipSlotEnum::Shoulders)
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
                    u32 glovesID = unitEquipment->equipmentSlotToVisualItemID[(u32)Generated::ItemEquipSlotEnum::Gloves];
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

                u32 shirtID = unitEquipment->equipmentSlotToVisualItemID[(u32)Generated::ItemEquipSlotEnum::Shirt];
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

                u32 bracersID = unitEquipment->equipmentSlotToVisualItemID[(u32)Generated::ItemEquipSlotEnum::Bracers];
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

                u32 bootsID = unitEquipment->equipmentSlotToVisualItemID[(u32)Generated::ItemEquipSlotEnum::Boots];
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

                u32 pantsID = unitEquipment->equipmentSlotToVisualItemID[(u32)Generated::ItemEquipSlotEnum::Pants];
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

                u32 chestID = unitEquipment->equipmentSlotToVisualItemID[(u32)Generated::ItemEquipSlotEnum::Chest];
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
                    u32 glovesID = unitEquipment->equipmentSlotToVisualItemID[(u32)Generated::ItemEquipSlotEnum::Gloves];
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

                u32 beltID = unitEquipment->equipmentSlotToVisualItemID[(u32)Generated::ItemEquipSlotEnum::Belt];
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

                u32 tabardID = unitEquipment->equipmentSlotToVisualItemID[(u32)Generated::ItemEquipSlotEnum::Tabard];
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