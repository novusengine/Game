#include "UpdateUnitEntities.h"

#include "Game-Lib/ECS/Components/AnimationData.h"
#include "Game-Lib/ECS/Components/DisplayInfo.h"
#include "Game-Lib/ECS/Components/Events.h"
#include "Game-Lib/ECS/Components/Model.h"
#include "Game-Lib/ECS/Components/MovementInfo.h"
#include "Game-Lib/ECS/Components/Name.h"
#include "Game-Lib/ECS/Components/Unit.h"
#include "Game-Lib/ECS/Components/UnitEquipment.h"
#include "Game-Lib/ECS/Components/UnitMovementOverTime.h"
#include "Game-Lib/ECS/Components/UnitStatsComponent.h"
#include "Game-Lib/ECS/Singletons/CharacterSingleton.h"
#include "Game-Lib/ECS/Singletons/JoltState.h"
#include "Game-Lib/ECS/Singletons/Database/ClientDBSingleton.h"
#include "Game-Lib/ECS/Util/Transforms.h"
#include "Game-Lib/Gameplay/Animation/Defines.h"
#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Rendering/Model/ModelLoader.h"
#include "Game-Lib/Util/AnimationUtil.h"
#include "Game-Lib/Util/ServiceLocator.h"
#include "Game-Lib/Util/UnitUtil.h"

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

        ModelLoader* modelLoader = ServiceLocator::GetGameRenderer()->GetModelLoader();

        auto modelLoadedEventView = registry.view<Components::ModelLoadedEvent>();
        modelLoadedEventView.each([&](entt::entity entity, Components::ModelLoadedEvent& modelLoadedEvent)
        {
            auto& model = registry.get<Components::Model>(entity);
            auto* name = registry.try_get<Components::Name>(entity);

            auto& discoveredModel = modelLoader->GetDiscoveredModel(model.modelHash);
            NC_LOG_INFO("Entity \"{0}\" Loaded New Model \"{1}\"", name->name, discoveredModel.name);

            if (modelLoadedEvent.flags.loaded)
            {
                modelLoader->DisableAllGroupsForModel(model);
            }

            if (auto* modelQueuedGeometryGroups = registry.try_get<Components::ModelQueuedGeometryGroups>(entity))
            {
                if (modelLoadedEvent.flags.loaded)
                {
                    for (u32 groupID : modelQueuedGeometryGroups->enabledGroupIDs)
                    {
                        modelLoader->EnableGroupForModel(model, groupID);
                    }
                }

                registry.erase<Components::ModelQueuedGeometryGroups>(entity);
            }

            if (auto* unitEquipment = registry.try_get<Components::UnitEquipment>(entity))
            {
                if (modelLoadedEvent.flags.loaded)
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
        });
        registry.clear<Components::ModelLoadedEvent>();

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
                Util::Animation::SetBoneRotation(modelInfo, animationData, ::Animation::Defines::Bone::SpineLow, rotation);
            }
            if (HandleUpdateOrientation(movementInfo.headRotationSettings, deltaTime))
            {
                auto& animationData = registry.get<Components::AnimationData>(entity);

                quat rotation = glm::quat(glm::vec3(0.0f, glm::radians(movementInfo.headRotationSettings.x), 0.0f));
                Util::Animation::SetBoneRotation(modelInfo, animationData, ::Animation::Defines::Bone::Head, rotation);
            }
            if (HandleUpdateOrientation(movementInfo.rootRotationSettings, deltaTime))
            {
                auto& animationData = registry.get<Components::AnimationData>(entity);

                quat rotation = glm::quat(glm::vec3(0.0f, glm::radians(movementInfo.rootRotationSettings.x), 0.0f));
                Util::Animation::SetBoneRotation(modelInfo, animationData, ::Animation::Defines::Bone::Default, rotation);
            }
        });

        auto& clientDBSingleton = ServiceLocator::GetEnttRegistries()->dbRegistry->ctx().get<Singletons::Database::ClientDBSingleton>();
        auto* itemStorage = clientDBSingleton.Get(ClientDBHash::Item);

        auto unitEquipmentView = registry.view<const Components::Unit, const Components::DisplayInfo, Components::UnitEquipment, Components::UnitEquipmentDirty>();
        unitEquipmentView.each([&](entt::entity entity, const Components::Unit& unit, const Components::DisplayInfo& displayInfo, Components::UnitEquipment& unitEquipment)
        {
            bool needToRefreshGeometry = false;

            for (const Database::Item::ItemEquipSlot equipSlot : unitEquipment.dirtyEquipmentSlots)
            {
                u32 itemID = unitEquipment.equipmentSlotToItemID[(u32)equipSlot];

                needToRefreshGeometry |= equipSlot != ::Database::Item::ItemEquipSlot::MainHand && equipSlot != ::Database::Item::ItemEquipSlot::OffHand && equipSlot != ::Database::Item::ItemEquipSlot::Ranged;

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
                    auto& item = itemStorage->Get<Database::Item::Item>(itemID);

                    if (equipSlot == ::Database::Item::ItemEquipSlot::MainHand)
                    {
                        entt::entity itemEntity = entt::null;

                        if (::Util::Unit::AddWeaponToHand(registry, characterSingleton.moverEntity, item, false, itemEntity))
                            ::Util::Unit::CloseHand(registry, characterSingleton.moverEntity, false);
                    }
                    else if (equipSlot == ::Database::Item::ItemEquipSlot::OffHand)
                    {
                        entt::entity itemEntity = entt::null;

                        if (::Util::Unit::AddWeaponToHand(registry, characterSingleton.moverEntity, item, true, itemEntity))
                            ::Util::Unit::CloseHand(registry, characterSingleton.moverEntity, true);
                    }
                    else if (equipSlot == ::Database::Item::ItemEquipSlot::Helm)
                    {
                        entt::entity itemEntity = entt::null;

                        ::Util::Unit::AddHelm(registry, characterSingleton.moverEntity, item, displayInfo.race, displayInfo.gender, itemEntity);
                    }
                    else if (equipSlot == ::Database::Item::ItemEquipSlot::Shoulders)
                    {
                        entt::entity shoulderLeftEntity = entt::null;
                        entt::entity shoulderRightEntity = entt::null;

                        ::Util::Unit::AddShoulders(registry, characterSingleton.moverEntity, item, shoulderLeftEntity, shoulderRightEntity);
                    }
                }
            }

            if (needToRefreshGeometry)
            {
                auto& model = registry.get<Components::Model>(entity);

                ::Util::Unit::DisableAllGeometryGroups(registry, entity, model);
                ::Util::Unit::RefreshGeometryGroups(registry, entity, model);
            }
            unitEquipment.dirtyEquipmentSlots.clear();
        });
        
        registry.clear<Components::UnitEquipmentDirty>();
    }
}