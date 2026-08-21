#include "NetworkConnection.h"
#include "CharacterController.h"
#include "OrbitalCamera.h"

#include "Game-Lib/ECS/Components/AABB.h"
#include "Game-Lib/ECS/Components/AnimationData.h"
#include "Game-Lib/ECS/Components/AttachmentData.h"
#include "Game-Lib/ECS/Components/Camera.h"
#include "Game-Lib/ECS/Components/Container.h"
#include "Game-Lib/ECS/Components/CastInfo.h"
#include "Game-Lib/ECS/Components/DisplayInfo.h"
#include "Game-Lib/ECS/Components/Events.h"
#include "Game-Lib/ECS/Components/Model.h"
#include "Game-Lib/ECS/Components/MovementInfo.h"
#include "Game-Lib/ECS/Components/Name.h"
#include "Game-Lib/ECS/Components/Item.h"
#include "Game-Lib/ECS/Components/InteractionCapabilities.h"
#include "Game-Lib/ECS/Components/ProximityTrigger.h"
#include "Game-Lib/ECS/Components/Tags.h"
#include "Game-Lib/ECS/Components/Unit.h"
#include "Game-Lib/ECS/Components/UnitAuraInfo.h"
#include "Game-Lib/ECS/Components/UnitCustomization.h"
#include "Game-Lib/ECS/Components/UnitEquipment.h"
#include "Game-Lib/ECS/Components/UnitFaction.h"
#include "Game-Lib/ECS/Components/UnitMovementOverTime.h"
#include "Game-Lib/ECS/Components/UnitPowersComponent.h"
#include "Game-Lib/ECS/Components/UnitResistancesComponent.h"
#include "Game-Lib/ECS/Components/UnitStatsComponent.h"
#include "Game-Lib/ECS/Singletons/CharacterSingleton.h"
#include "Game-Lib/ECS/Singletons/CharacterControllerSingleton.h"
#include "Game-Lib/ECS/Singletons/JoltState.h"
#include "Game-Lib/ECS/Singletons/NetworkState.h"
#include "Game-Lib/ECS/Singletons/OrbitalCameraSettings.h"
#include "Game-Lib/ECS/Util/CameraUtil.h"
#include "Game-Lib/ECS/Singletons/ProximityTriggerSingleton.h"
#include "Game-Lib/ECS/Singletons/Database/ClientDBSingleton.h"
#include "Game-Lib/ECS/Singletons/Database/SpellSingleton.h"
#include "Game-Lib/ECS/Util/EventUtil.h"
#include "Game-Lib/ECS/Util/FactionUtil.h"
#include "Game-Lib/ECS/Util/MessageBuilderUtil.h"
#include "Game-Lib/ECS/Util/ProximityTriggerUtil.h"
#include "Game-Lib/ECS/Util/Transforms.h"
#include "Game-Lib/ECS/Util/Database/SpellUtil.h"
#include "Game-Lib/ECS/Util/Network/NetworkUtil.h"
#include "Game-Lib/Editor/SpellEditorBackend.h"
#include "Game-Lib/Editor/CreatureAIEditorBackend.h"
#include "Game-Lib/Editor/MapEditorBackend.h"
#include "Game-Lib/Editor/MapEditorData.h"
#include "Game-Lib/Editor/InteractionEditorBackend.h"
#include "Game-Lib/Editor/InteractionEditorData.h"
#include "Game-Lib/Editor/SpellEditorData.h"
#include "Game-Lib/Gameplay/MapLoader.h"
#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Rendering/Debug/DebugRenderer.h"
#include "Game-Lib/Rendering/Model/ModelLoader.h"
#include "Game-Lib/Scripting/Util/ZenithUtil.h"
#include "Game-Lib/Util/ServiceLocator.h"
#include "Game-Lib/Util/UnitUtil.h"

#include <Base/CVarSystem/CVarSystem.h>
#include <Base/Util/DebugHandler.h>

#include <Gameplay/ECS/Components/ObjectFields.h>
#include <Gameplay/ECS/Components/UnitFields.h>
#include <Gameplay/Network/GameMessageRouter.h>

#include <MetaGen/Shared/ProximityTrigger/ProximityTrigger.h>

#include <Network/Client.h>
#include <Network/Define.h>

#include <MetaGen/EnumTraits.h>
#include <MetaGen/Game/Lua/Lua.h>
#include <MetaGen/Shared/ClientDB/ClientDB.h>
#include <MetaGen/Shared/CombatLog/CombatLog.h>
#include <MetaGen/Shared/DatabaseEditor/DatabaseEditor.h>
#include <MetaGen/Shared/Interaction/Interaction.h>
#include <MetaGen/Shared/Packet/Packet.h>
#include <MetaGen/Shared/Spell/Spell.h>
#include <MetaGen/Shared/Unit/Unit.h>

#include <Scripting/LuaManager.h>
#include <Scripting/Zenith.h>

#include <entt/entt.hpp>
#include <imgui/ImGuiNotify.hpp>
#include <libsodium/sodium.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <numeric>
#include <utility>

AutoCVar_Int CVAR_NetworkDirectRemoteUnitPosition(CVarCategory::Network, "directRemoteUnitPosition", "Applies remote unit movement packet positions directly instead of interpolating", 0, CVarFlags::EditCheckbox | CVarFlags::DoNotSave);

namespace ECS::Systems
{
    static u64 CalculateAuraExpirationTimestamp(f32 duration)
    {
        if (duration == -1.0f)
            return 0;

        const u64 currentTime = static_cast<u64>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
        if (!std::isfinite(duration) || duration <= 0.0f)
            return currentTime;

        const f64 durationMilliseconds = static_cast<f64>(duration) * 1000.0;
        const f64 maximumDurationMilliseconds = static_cast<f64>(std::numeric_limits<u64>::max() - currentTime);
        if (durationMilliseconds >= maximumDurationMilliseconds)
            return std::numeric_limits<u64>::max();

        return currentTime + static_cast<u64>(durationMilliseconds);
    }

    enum class AutoAttackWeaponSlot
    {
        None,
        MainHand,
        OffHand
    };

    static AutoAttackWeaponSlot GetAutoAttackWeaponSlot(u32 spellID)
    {
        entt::registry* dbRegistry = ServiceLocator::GetEnttRegistries()->dbRegistry;
        auto& dbContext = dbRegistry->ctx();
        auto& clientDBSingleton = dbContext.get<Singletons::ClientDBSingleton>();
        auto& spellSingleton = dbContext.get<Singletons::SpellSingleton>();

        const std::vector<u32>* effectList = ECSUtil::Spell::GetSpellEffectList(spellSingleton, spellID);
        if (!effectList || effectList->empty())
            return AutoAttackWeaponSlot::None;

        auto* effectStorage = clientDBSingleton.Get(ClientDBHash::SpellEffects);
        const auto& effect = effectStorage->Get<MetaGen::Shared::ClientDB::SpellEffectsRecord>(effectList->front());
        if (static_cast<MetaGen::Shared::Spell::SpellEffectTypeEnum>(effect.effectType) != MetaGen::Shared::Spell::SpellEffectTypeEnum::WeaponDamage)
            return AutoAttackWeaponSlot::None;

        constexpr i32 MAIN_HAND_WEAPON_SLOT = 1;
        constexpr i32 OFF_HAND_WEAPON_SLOT = 2;
        if (effect.parameters[1] == MAIN_HAND_WEAPON_SLOT)
            return AutoAttackWeaponSlot::MainHand;
        if (effect.parameters[1] == OFF_HAND_WEAPON_SLOT)
            return AutoAttackWeaponSlot::OffHand;

        return AutoAttackWeaponSlot::None;
    }

    static u32 GetEquippedItemIDForAnimation(const Components::UnitEquipment& equipment, MetaGen::Shared::Unit::ItemEquipSlotEnum slot)
    {
        const u32 slotIndex = static_cast<u32>(slot);
        const u32 itemID = equipment.equipmentSlotToItemID[slotIndex];
        return itemID != 0 ? itemID : equipment.equipmentSlotToVisualItemID[slotIndex];
    }

    static bool QueueUnitAttackAnimation(entt::registry& registry, entt::entity entity, AutoAttackWeaponSlot weaponSlot)
    {
        if (weaponSlot == AutoAttackWeaponSlot::None)
            return false;

        auto* unit = registry.try_get<Components::Unit>(entity);
        auto* equipment = registry.try_get<Components::UnitEquipment>(entity);
        auto* model = registry.try_get<Components::Model>(entity);
        if (!unit || !equipment || !model)
            return false;

        const auto equipmentSlot = weaponSlot == AutoAttackWeaponSlot::OffHand
            ? MetaGen::Shared::Unit::ItemEquipSlotEnum::OffHand
            : MetaGen::Shared::Unit::ItemEquipSlotEnum::MainHand;
        const u32 itemID = GetEquippedItemIDForAnimation(*equipment, equipmentSlot);
        if (weaponSlot == AutoAttackWeaponSlot::OffHand && itemID == 0)
            return false;

        entt::registry* dbRegistry = ServiceLocator::GetEnttRegistries()->dbRegistry;
        auto& clientDBSingleton = dbRegistry->ctx().get<Singletons::ClientDBSingleton>();
        auto* itemStorage = clientDBSingleton.Get(ClientDBHash::Item);
        const auto& itemTemplate = itemStorage->Get<MetaGen::Shared::ClientDB::ItemRecord>(itemID);

        if (!ServiceLocator::GetGameRenderer()->GetModelLoader()->GetModelInfo(model->modelHash))
            return false;

        unit->attackReadyAnimation = ::Util::Unit::GetAttackReadyAnimation(itemTemplate.categoryType);
        if (weaponSlot == AutoAttackWeaponSlot::OffHand)
        {
            unit->attackOffHandAnimation = ::Util::Unit::GetOffHandAttackAnimation(itemTemplate.categoryType);
        }
        else
        {
            unit->attackMainHandAnimation = ::Util::Unit::GetMainHandAttackAnimation(itemTemplate.categoryType);
        }

        return true;
    }

    static void EmitUnitReactionChanged(entt::entity entity, Gameplay::Faction::Reaction oldReaction, Gameplay::Faction::Reaction newReaction)
    {
        Scripting::Zenith* zenith = Scripting::Util::Zenith::GetGlobal();
        if (!zenith)
            return;

        zenith->CallEvent(MetaGen::Game::Lua::UnitEvent::ReactionChanged, MetaGen::Game::Lua::UnitEventDataReactionChanged{ .unitID = entt::to_integral(entity), .oldReaction = static_cast<u8>(oldReaction), .newReaction = static_cast<u8>(newReaction) });
    }

    static void EmitReputationChanged(const Gameplay::Faction::ReputationChange& change)
    {
        Scripting::Zenith* zenith = Scripting::Util::Zenith::GetGlobal();
        if (!zenith)
            return;

        zenith->CallEvent(MetaGen::Game::Lua::ReputationEvent::Changed, MetaGen::Game::Lua::ReputationEventDataChanged
        {
            .factionID = change.factionID,
            .oldValue = change.oldValue,
            .newValue = change.newValue,
            .oldFlags = change.oldFlags,
            .newFlags = change.newFlags,
            .oldPersistentStandingID = change.oldPersistentStandingID,
            .newPersistentStandingID = change.newPersistentStandingID,
            .oldEffectiveStandingID = change.oldEffectiveStandingID,
            .newEffectiveStandingID = change.newEffectiveStandingID,
            .oldPerceptionFields = change.oldPerceptionFields,
            .newPerceptionFields = change.newPerceptionFields,
            .wasPresent = change.wasPresent,
            .isPresent = change.isPresent
        });
    }

    static void InitActiveCharacterController(entt::registry& registry, bool isLocal)
    {
        CharacterController::InitCharacterController(registry, isLocal);
    }

    static void DeleteActiveCharacterController(entt::registry& registry, bool isLocal)
    {
        CharacterController::DeleteCharacterController(registry, isLocal);
    }

    static void CleanupCharacterContainers(entt::registry& registry)
    {
        auto& characterSingleton = registry.ctx().get<Singletons::CharacterSingleton>();

        // The base container has no network GUID and is therefore not part of
        // NetworkState::entityToNetworkID. Clean it up explicitly between worlds.
        if (registry.valid(characterSingleton.baseContainerEntity))
            registry.destroy(characterSingleton.baseContainerEntity);

        characterSingleton.baseContainerEntity = entt::null;
        characterSingleton.containers.fill(ObjectGUID::Empty);
    }

    static void CleanupOrbitalCameraState(entt::registry& registry)
    {
        auto* settings = registry.ctx().find<Singletons::OrbitalCameraSettings>();
        if (!settings)
            return;

        // The orbital camera and controller are application-lifetime entities,
        // while the mover/world are session-lifetime. Do not carry a parent
        // relationship or transient collision/input state into the next world.
        if (registry.valid(settings->entity))
            TransformSystem::Get(registry).ClearParent(settings->entity);

        if (settings->captureMouse)
            Util::CameraUtil::SetCaptureMouse(false, settings->captureRestoreMousePosition);

        settings->captureMouse = false;
        settings->captureMousePending = false;
        settings->captureMouseHasMoved = false;
        settings->captureMouseWasDragged = false;
        settings->mouseLeftDown = false;
        settings->mouseRightDown = false;
        settings->cameraCollisionCurrentDistance = -1.0f;
        settings->cameraCollisionWasObstructed = false;
    }

    static void CleanupNetworkProximityTriggers(entt::registry& registry)
    {
        auto& proximityTriggerSingleton = registry.ctx().get<Singletons::ProximityTriggerSingleton>();

        std::vector<entt::entity> triggerEntities;
        auto triggerView = registry.view<Components::ProximityTrigger>();
        triggerView.each([&triggerEntities](entt::entity triggerEntity, Components::ProximityTrigger& trigger)
        {
            if (trigger.networkID != Components::ProximityTrigger::INVALID_NETWORK_ID)
                triggerEntities.push_back(triggerEntity);
        });

        // Destroy entities directly so duplicate/orphaned trigger IDs from an
        // interrupted previous teardown are repaired as well.
        for (entt::entity triggerEntity : triggerEntities)
        {
            proximityTriggerSingleton.proximityTriggers.Remove(triggerEntity);
            registry.destroy(triggerEntity);
        }

        // Clear stale membership and ID entries after removing every live entity.
        proximityTriggerSingleton.triggerIDToEntity.clear();
        proximityTriggerSingleton.entityToProximityTriggers.clear();
    }

    static void CleanupNetworkWorldEntities(entt::registry& registry)
    {
        auto& networkState = registry.ctx().get<Singletons::NetworkState>();

        DeleteActiveCharacterController(registry, false);
        CleanupOrbitalCameraState(registry);
        CleanupNetworkProximityTriggers(registry);

        for (auto& [entity, networkID] : networkState.entityToNetworkID)
        {
            if (!registry.valid(entity))
                continue;

            if (auto* attachmentData = registry.try_get<Components::AttachmentData>(entity))
            {
                for (auto& pair : attachmentData->attachmentToInstance)
                    ::Util::Unit::RemoveItemFromAttachment(registry, entity, pair.first);
            }

            if (auto* model = registry.try_get<Components::Model>(entity))
            {
                if (model->instanceID != std::numeric_limits<u32>().max())
                    ServiceLocator::GetGameRenderer()->GetModelLoader()->UnloadModelForEntity(entity, *model);
            }

            registry.destroy(entity);
        }

        CleanupCharacterContainers(registry);

        networkState.isLoadingMap = false;
        networkState.isInWorld = false;
        networkState.pathToVisualize.clear();
        networkState.interactionState.Reset();
        networkState.entityToNetworkID.clear();
        networkState.networkIDToEntity.clear();
        networkState.networkVisTree->RemoveAll();
    }

    bool HandleOnCharacterList(Network::SocketID socketID, Network::Message& message)
    {
        entt::registry* gameRegistry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& networkState = gameRegistry->ctx().get<Singletons::NetworkState>();

        u8 numCharacters = 0;
        if (!message.buffer->GetU8(numCharacters))
            return false;

        std::vector<CharacterListEntry> characterList;
        characterList.reserve(numCharacters);

        bool failed = false;

        for (u8 i = 0; i < numCharacters; i++)
        {
            CharacterListEntry& entry = characterList.emplace_back();

            failed |= !message.buffer->GetString(entry.name);
            failed |= !message.buffer->GetU8(entry.race);
            failed |= !message.buffer->GetU8(entry.gender);
            failed |= !message.buffer->GetU8(entry.unitClass);
            failed |= !message.buffer->GetU16(entry.level);
            failed |= !message.buffer->GetU32(entry.mapID);
        }

        if (failed)
            return false;

        networkState.characterListInfo.list = std::move(characterList);

        networkState.characterListInfo.nameHashToIndex.reserve(numCharacters);
        networkState.characterListInfo.nameHashToIndex.clear();
        for (u32 i = 0; i < numCharacters; i++)
        {
            const CharacterListEntry& entry = networkState.characterListInfo.list[i];

            u32 nameHash = StringUtils::fnv1a_32(entry.name.c_str(), entry.name.length());
            networkState.characterListInfo.nameHashToIndex[nameHash] = i;
        }

        for (auto itr = networkState.characterListInfo.nameHashToSortingIndex.begin(); itr != networkState.characterListInfo.nameHashToSortingIndex.end();)
        {
            u32 nameHash = itr->first;

            bool characterNoLongerExists = networkState.characterListInfo.nameHashToIndex.find(nameHash) == networkState.characterListInfo.nameHashToIndex.end();
            if (characterNoLongerExists)
            {
                itr = networkState.characterListInfo.nameHashToSortingIndex.erase(itr);
            }
            else
            {
                ++itr;
            }
        }

        if (!networkState.isInWorld && networkState.authInfo.stage == AuthenticationStage::Completed)
        {
            Scripting::Zenith* zenith = Scripting::Util::Zenith::GetGlobal();
            zenith->CallEvent(MetaGen::Game::Lua::GameEvent::CharacterListChanged, MetaGen::Game::Lua::GameEventDataCharacterListChanged{});
        }

        return true;
    }

    bool HandleOnObjectNetFieldUpdate(Network::SocketID socketID, Network::Message& message)
    {
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& networkState = registry->ctx().get<Singletons::NetworkState>();

        ObjectGUID objectGUID;
        if (!message.buffer->Deserialize(objectGUID))
            return false;

        entt::entity entity;
        if (!Util::Network::GetEntityIDFromObjectGUID(networkState, objectGUID, entity))
        {
            NC_LOG_WARNING("Network : Received Object NetField Update for non existing entity ({0})", objectGUID.ToString());
            return true;
        }

        if (!registry->valid(entity))
        {
            NC_LOG_WARNING("Network : Received Object NetField Update for non existing entity ({0})", objectGUID.ToString());
            return true;
        }

        u8 byteMaskOffset = 0;
        u8 numMaskBytes = 0;

        if (!message.buffer->GetU8(byteMaskOffset))
            return false;

        if (!message.buffer->GetU8(numMaskBytes))
            return false;

        std::vector<u8> maskBytes(numMaskBytes);
        if (!message.buffer->GetBytes(maskBytes.data(), numMaskBytes))
            return false;

        auto& objectFields = registry->get<Components::ObjectFields>(entity);

        // Apply NetField Updates
        for (u32 i = 0; i < numMaskBytes; i++)
        {
            u8 maskByte = maskBytes[i];

            while (maskByte)
            {
                u16 bitIndex = static_cast<u16>(std::countr_zero(maskByte));
                maskByte &= (maskByte - 1);

                u16 fieldID = static_cast<u16>(byteMaskOffset * 8 + bitIndex);
                u32 data = 0;

                if (!message.buffer->GetU32(data))
                {
                    NC_LOG_WARNING("Network : Failed to read Object NetField Update data for entity ({0}) fieldID ({1})", objectGUID.ToString(), fieldID);
                    return false;
                }

                auto objectField = static_cast<MetaGen::Shared::NetField::ObjectNetFieldEnum>(fieldID);
                objectFields.fields.SetField(objectField, data);
            }
        }

        // Call Field Update Callback Handlers
        for (u32 i = 0; i < numMaskBytes; i++)
        {
            u8 maskByte = maskBytes[i];

            while (maskByte)
            {
                u16 bitIndex = static_cast<u16>(std::countr_zero(maskByte));
                maskByte &= (maskByte - 1);

                u16 fieldID = static_cast<u16>(byteMaskOffset * 8 + bitIndex);
                auto objectField = static_cast<MetaGen::Shared::NetField::ObjectNetFieldEnum>(fieldID);
                networkState.objectNetFieldListener.NotifyFieldChanged(entity, objectGUID, objectField);
            }
        }

        return true;
    }
    bool HandleOnUnitNetFieldUpdate(Network::SocketID socketID, Network::Message& message)
    {
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& networkState = registry->ctx().get<Singletons::NetworkState>();

        ObjectGUID objectGUID;
        if (!message.buffer->Deserialize(objectGUID))
            return false;

        entt::entity entity;
        if (!Util::Network::GetEntityIDFromObjectGUID(networkState, objectGUID, entity))
        {
            NC_LOG_WARNING("Network : Received Unit NetField Update for non existing entity ({0})", objectGUID.ToString());
            return true;
        }

        if (!registry->valid(entity))
        {
            NC_LOG_WARNING("Network : Received Unit NetField Update for non existing entity ({0})", objectGUID.ToString());
            return true;
        }

        u8 byteMaskOffset = 0;
        u8 numMaskBytes = 0;

        if (!message.buffer->GetU8(byteMaskOffset))
            return false;

        if (!message.buffer->GetU8(numMaskBytes))
            return false;

        std::vector<u8> maskBytes(numMaskBytes);
        if (!message.buffer->GetBytes(maskBytes.data(), numMaskBytes))
            return false;

        auto& unitFields = registry->get<Components::UnitFields>(entity);

        // Apply NetField Updates
        for (u32 i = 0; i < numMaskBytes; i++)
        {
            u8 maskByte = maskBytes[i];

            while (maskByte)
            {
                u16 bitIndex = static_cast<u16>(std::countr_zero(maskByte));
                maskByte &= (maskByte - 1);

                u16 fieldID = static_cast<u16>(byteMaskOffset * 8 + bitIndex);
                u32 data = 0;

                if (!message.buffer->GetU32(data))
                {
                    NC_LOG_WARNING("Network : Failed to read Unit NetField Update data for entity ({0}) fieldID ({1})", objectGUID.ToString(), fieldID);
                    return false;
                }

                auto unitField = static_cast<MetaGen::Shared::NetField::UnitNetFieldEnum>(fieldID);
                unitFields.fields.SetField(unitField, data);
            }
        }

        // Call Field Update Callback Handlers
        for (u32 i = 0; i < numMaskBytes; i++)
        {
            u8 maskByte = maskBytes[i];

            while (maskByte)
            {
                u16 bitIndex = static_cast<u16>(std::countr_zero(maskByte));
                maskByte &= (maskByte - 1);

                u16 fieldID = static_cast<u16>(byteMaskOffset * 8 + bitIndex);
                auto unitField = static_cast<MetaGen::Shared::NetField::UnitNetFieldEnum>(fieldID);
                networkState.unitNetFieldListener.NotifyFieldChanged(entity, objectGUID, unitField);
            }
        }

        return true;
    }
    bool HandleOnCombatEvent(Network::SocketID socketID, Network::Message& message)
    {
        MetaGen::Shared::CombatLog::CombatLogEventEnum eventID;
        ObjectGUID sourceNetworkID;

        if (!message.buffer->Get(eventID))
            return false;

        if (!message.buffer->Deserialize(sourceNetworkID))
            return false;

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& networkState = registry->ctx().get<Singletons::NetworkState>();

        entt::entity sourceEntity;
        if (!Util::Network::GetEntityIDFromObjectGUID(networkState, sourceNetworkID, sourceEntity))
        {
            NC_LOG_WARNING("Network : Received Combat Event for non existing entity ({0})", sourceNetworkID.ToString());
            return true;
        }

        if (!registry->valid(sourceEntity))
        {
            NC_LOG_WARNING("Network : Received Combat Event for non existing entity ({0})", sourceNetworkID.ToString());
            return true;
        }

        switch (eventID)
        {
            // Damage Taken
            case MetaGen::Shared::CombatLog::CombatLogEventEnum::DamageDealt:
            case MetaGen::Shared::CombatLog::CombatLogEventEnum::HealingDone:
            {
                ObjectGUID targetNetworkID;
                f64 value = 0.0f;
                f64 overValue = 0.0f;

                if (!message.buffer->Deserialize(targetNetworkID))
                    return false;

                if (!message.buffer->GetF64(value))
                    return false;

                if (!message.buffer->GetF64(overValue))
                    return false;

                entt::entity targetEntity;
                if (!Util::Network::GetEntityIDFromObjectGUID(networkState, targetNetworkID, targetEntity))
                {
                    NC_LOG_WARNING("Network : Received Combat Event for non existing target entity ({0})", targetNetworkID.ToString());
                    return true;
                }

                 if (!registry->valid(targetEntity))
                 {
                     NC_LOG_WARNING("Network : Received Combat Event for non existing target entity ({0})", targetNetworkID.ToString());
                     return true;
                 }

                 auto* sourceUnit = registry->try_get<Components::Unit>(sourceEntity);
                 auto* targetUnit = registry->try_get<Components::Unit>(targetEntity);

                 if (!sourceUnit || !targetUnit)
                 {
                     NC_LOG_WARNING("Network : Received Combat Event for entity without Unit Component ({0})", targetNetworkID.ToString());
                     return true;
                 }

                 if (sourceUnit->name.empty() || targetUnit->name.empty())
                 {
                     return true;
                 }

                 {
                     Scripting::Zenith* zenith = Scripting::Util::Zenith::GetGlobal();
                     zenith->CallEvent(MetaGen::Game::Lua::GameEvent::CombatLog, MetaGen::Game::Lua::GameEventDataCombatLog{ .eventID = static_cast<u16>(eventID), .sourceName = sourceUnit->name, .targetName = targetUnit->name, .value1 = value, .value2 = overValue });
                 }
                 break;
             }

             case MetaGen::Shared::CombatLog::CombatLogEventEnum::Resurrected:
             {
                 ObjectGUID targetNetworkID;
                 f64 restoredHealth = 0.0f;

                 if (!message.buffer->Deserialize(targetNetworkID))
                     return false;

                 if (!message.buffer->GetF64(restoredHealth))
                     return false;

                 entt::entity targetEntity;
                 if (!Util::Network::GetEntityIDFromObjectGUID(networkState, targetNetworkID, targetEntity))
                 {
                     NC_LOG_WARNING("Network : Received Combat Event for non existing target entity ({0})", targetNetworkID.ToString());
                     return true;
                 }

                 if (!registry->valid(targetEntity))
                 {
                     NC_LOG_WARNING("Network : Received Combat Event for non existing target entity ({0})", targetNetworkID.ToString());
                     return true;
                 }

                 auto* sourceUnit = registry->try_get<Components::Unit>(sourceEntity);
                 auto* targetUnit = registry->try_get<Components::Unit>(targetEntity);

                 if (!sourceUnit || !targetUnit)
                 {
                     NC_LOG_WARNING("Network : Received Combat Event for entity without Unit Component ({0})", targetNetworkID.ToString());
                     return true;
                 }

                 if (sourceUnit->name.empty() || targetUnit->name.empty())
                 {
                     return true;
                 }

                 {
                     Scripting::Zenith* zenith = Scripting::Util::Zenith::GetGlobal();
                     zenith->CallEvent(MetaGen::Game::Lua::GameEvent::CombatLog, MetaGen::Game::Lua::GameEventDataCombatLog{ .eventID = static_cast<u16>(MetaGen::Shared::CombatLog::CombatLogEventEnum::Resurrected), .sourceName = sourceUnit->name, .targetName = targetUnit->name, .value1 = restoredHealth, .value2 = 0.0 });
                 }
                 break;
            }

            default:
            {
                break;
            }
        }

        return true;
    }
    bool HandleOnVisualizePath(Network::SocketID socketID, Network::Message& message)
    {
        u32 numPaths;
        if (!message.buffer->GetU32(numPaths))
            return false;

        std::vector<vec3> positions(numPaths);
        if (!message.buffer->GetBytes(positions.data(), numPaths * sizeof(vec3)))
            return false;

        entt::registry* gameRegistry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& networkState = gameRegistry->ctx().get<Singletons::NetworkState>();

        networkState.pathToVisualize.resize(numPaths);
        memcpy(networkState.pathToVisualize.data(), positions.data(), numPaths * sizeof(vec3));

        return true;
    }

    Editor::DatabaseEditorData* GetDatabaseEditorData(MetaGen::Shared::DatabaseEditor::DatabaseEditorTypeEnum editor, bool create)
    {
        entt::registry& registry = *ServiceLocator::GetEnttRegistries()->dbRegistry;
        auto& context = registry.ctx();
        using EditorType = MetaGen::Shared::DatabaseEditor::DatabaseEditorTypeEnum;
        if (editor == EditorType::Spell)
        {
            if (context.contains<Editor::SpellEditorData>())
                return &context.get<Editor::SpellEditorData>();

            return create ? &context.emplace<Editor::SpellEditorData>() : nullptr;
        }
        if (editor == EditorType::Map)
        {
            if (context.contains<Editor::MapEditorData>())
                return &context.get<Editor::MapEditorData>();

            return create ? &context.emplace<Editor::MapEditorData>() : nullptr;
        }
        if (editor == EditorType::Interaction)
        {
            if (context.contains<Editor::InteractionEditorData>())
                return &context.get<Editor::InteractionEditorData>();

            return create ? &context.emplace<Editor::InteractionEditorData>() : nullptr;
        }

        return nullptr;
    }

    bool HandleOnDatabaseEditorSnapshotBegin(Network::SocketID, Network::Message& message)
    {
        u32 requestID = 0;
        MetaGen::Shared::DatabaseEditor::DatabaseEditorTypeEnum editor;
        u8 artifactCount = 0;
        u64 revision = 0;
        if (!message.buffer->GetU32(requestID) || !message.buffer->Get(editor) || !message.buffer->GetU8(artifactCount) || !message.buffer->GetU64(revision) || message.buffer->GetActiveSize() != 0 || editor >= MetaGen::Shared::DatabaseEditor::DatabaseEditorTypeEnum::Count)
        {
            return false;
        }

        Editor::DatabaseEditorData* editorData = GetDatabaseEditorData(editor, true);
        if (!editorData || !editorData->BeginSnapshot(requestID, artifactCount, revision))
        {
            if (editorData)
                editorData->FailSnapshot(requestID);
            return false;
        }

        return true;
    }

    bool HandleOnDatabaseEditorChangeSet(Network::SocketID, Network::Message& message)
    {
        using EditorType = MetaGen::Shared::DatabaseEditor::DatabaseEditorTypeEnum;
        EditorType editor;
        if (!message.buffer->Get(editor) || editor >= EditorType::Count)
            return false;

        Editor::DatabaseEditorData* editorData = GetDatabaseEditorData(editor, false);
        u64 revision = 0;
        u16 changeCount = 0;
        constexpr size_t MAX_CHANGE_SET_PACKET_PAYLOAD_SIZE = std::min<size_t>(std::numeric_limits<u16>::max(), Network::DEFAULT_BUFFER_SIZE - sizeof(Network::MessageHeader));
        constexpr size_t CHANGE_SET_HEADER_SIZE = sizeof(u8) + sizeof(u64) + sizeof(u16);
        constexpr size_t CHANGE_HEADER_SIZE = sizeof(u8) + sizeof(u8) + sizeof(u32) + sizeof(u32);
        constexpr size_t MAX_CHANGE_SET_BODY_SIZE = MAX_CHANGE_SET_PACKET_PAYLOAD_SIZE - CHANGE_SET_HEADER_SIZE;
        constexpr u16 MAX_CHANGE_SET_CHANGES = static_cast<u16>(MAX_CHANGE_SET_BODY_SIZE / CHANGE_HEADER_SIZE);
        if (!message.buffer->GetU64(revision) || !message.buffer->GetU16(changeCount) || changeCount == 0 || changeCount > MAX_CHANGE_SET_CHANGES)
        {
            if (editorData)
                editorData->FailChangeSet();
            return false;
        }

        const size_t payloadSize = message.buffer->GetActiveSize();
        if (payloadSize > MAX_CHANGE_SET_BODY_SIZE)
        {
            if (editorData)
                editorData->FailChangeSet();
            return false;
        }

        if (!editorData)
            return message.buffer->SkipRead(payloadSize);
        // Revision gaps and canonical apply failures are editor recovery events, not connection framing failures.
        editorData->ReceiveChangeSet(revision, changeCount, message.buffer->GetReadPointer(), payloadSize);
        return message.buffer->SkipRead(payloadSize);
    }

    bool HandleOnDatabaseEditorSnapshotChunk(Network::SocketID, Network::Message& message)
    {
        u32 requestID = 0;
        MetaGen::Shared::DatabaseEditor::DatabaseEditorTypeEnum editor;
        u8 typeValue = 0;
        u32 totalSize = 0;
        u32 offset = 0;
        u16 chunkSize = 0;
        if (!message.buffer->GetU32(requestID) || !message.buffer->Get(editor) || !message.buffer->GetU8(typeValue) || !message.buffer->GetU32(totalSize) || !message.buffer->GetU32(offset) || !message.buffer->GetU16(chunkSize))
        {
            return false;
        }

        constexpr size_t SNAPSHOT_CHUNK_HEADER_SIZE = sizeof(u32) + sizeof(u8) + sizeof(u8) + sizeof(u32) + sizeof(u32) + sizeof(u16);
        constexpr size_t MAX_SNAPSHOT_CHUNK_SIZE = Network::DEFAULT_BUFFER_SIZE - sizeof(Network::MessageHeader) - SNAPSHOT_CHUNK_HEADER_SIZE;
        if (editor >= MetaGen::Shared::DatabaseEditor::DatabaseEditorTypeEnum::Count || chunkSize == 0 || chunkSize > MAX_SNAPSHOT_CHUNK_SIZE)
            return false;

        std::vector<u8> bytes(chunkSize);
        if (!message.buffer->GetBytes(bytes.data(), bytes.size()))
            return false;

        Editor::DatabaseEditorData* editorData = GetDatabaseEditorData(editor, false);
        if (!editorData || !editorData->AppendSnapshotChunk(requestID, typeValue, totalSize, offset, bytes.data(), chunkSize))
        {
            if (editorData)
                editorData->FailSnapshot(requestID);
            return false;
        }

        return true;
    }

    bool HandleOnDatabaseEditorSnapshotEnd(Network::SocketID, Network::Message& message)
    {
        u32 requestID = 0;
        MetaGen::Shared::DatabaseEditor::DatabaseEditorTypeEnum editor;
        u8 succeeded = 0;
        if (!message.buffer->GetU32(requestID) || !message.buffer->Get(editor) || !message.buffer->GetU8(succeeded) || message.buffer->GetActiveSize() != 0 || succeeded > 1 || editor >= MetaGen::Shared::DatabaseEditor::DatabaseEditorTypeEnum::Count)
        {
            return false;
        }

        Editor::DatabaseEditorData* editorData = GetDatabaseEditorData(editor, false);
        if (!editorData)
            return false;

        const bool loaded = editorData->CompleteSnapshot(requestID, succeeded == 1);
        const char* notification = "The database editor snapshot completed.";
        switch (editor)
        {
            case MetaGen::Shared::DatabaseEditor::DatabaseEditorTypeEnum::Spell:
                notification = loaded ? "Spell editor data loaded from the server." : "The server rejected or failed the Spell editor snapshot.";
                break;
            case MetaGen::Shared::DatabaseEditor::DatabaseEditorTypeEnum::Map:
                notification = loaded ? "Map editor data loaded from the server." : "The server rejected or failed the Map editor snapshot.";
                break;
            case MetaGen::Shared::DatabaseEditor::DatabaseEditorTypeEnum::Interaction:
                notification = loaded ? "Interaction editor data loaded from the server." : "The server rejected or failed the Interaction editor snapshot.";
                break;
            default: break;
        }
        ImGui::InsertNotification({ loaded ? ImGuiToastType::Success : ImGuiToastType::Error, 3000, notification });
        return true;
    }

    bool HandleOnDatabaseEditorMutationResult(Network::SocketID, MetaGen::Shared::Packet::ServerDatabaseEditorMutationResultPacket& packet)
    {
        using EditorType = MetaGen::Shared::DatabaseEditor::DatabaseEditorTypeEnum;
        using MutationType = MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum;
        if (packet.editor >= static_cast<u8>(EditorType::Count) || packet.mutationType >= static_cast<u8>(MutationType::Count) || packet.succeeded > 1)
            return false;

        Editor::DatabaseEditorData* editorData = GetDatabaseEditorData(static_cast<EditorType>(packet.editor), true);
        if (!editorData)
            return false;

        editorData->RecordMutationResult({ .requestID = packet.requestID, .artifact = packet.artifact, .artifactID = packet.artifactID, .mutationType = static_cast<MutationType>(packet.mutationType), .succeeded = packet.succeeded == 1, .revision = packet.revision, .response = std::move(packet.response) });
        return true;
    }

    Editor::CreatureAIEditorBackend* GetCreatureAIEditorBackend()
    {
        EnttRegistries* registries = ServiceLocator::GetEnttRegistries();
        if (!registries || !registries->dbRegistry)
            return nullptr;

        auto& context = registries->dbRegistry->ctx();
        return context.contains<Editor::CreatureAIEditorBackend>()
            ? &context.get<Editor::CreatureAIEditorBackend>()
            : &context.emplace<Editor::CreatureAIEditorBackend>();
    }

    bool HandleOnDevelopmentActionResult(Network::SocketID, MetaGen::Shared::Packet::ServerDevelopmentActionResultPacket& packet)
    {
        Editor::CreatureAIEditorBackend* backend = GetCreatureAIEditorBackend();
        if (backend)
            backend->HandleActionResult(packet);
        return true;
    }

    bool HandleOnDevelopmentTransferBegin(Network::SocketID, Network::Message& message)
    {
        u32 requestID = 0;
        u32 totalSize = 0;
        u64 revision = 0;
        if (!message.buffer->GetU32(requestID) || !message.buffer->GetU32(totalSize) || !message.buffer->GetU64(revision))
            return false;

        Editor::CreatureAIEditorBackend* backend = GetCreatureAIEditorBackend();
        return backend && backend->BeginTransfer(requestID, totalSize, revision);
    }

    bool HandleOnDevelopmentTransferChunk(Network::SocketID, Network::Message& message)
    {
        u32 requestID = 0;
        u32 offset = 0;
        u16 chunkSize = 0;
        if (!message.buffer->GetU32(requestID) || !message.buffer->GetU32(offset) || !message.buffer->GetU16(chunkSize) || chunkSize == 0 || chunkSize > 1024)
        {
            return false;
        }

        std::vector<u8> bytes(chunkSize);
        if (!message.buffer->GetBytes(bytes.data(), bytes.size()))
            return false;

        Editor::CreatureAIEditorBackend* backend = GetCreatureAIEditorBackend();
        return backend && backend->AppendTransfer(requestID, offset, bytes.data(), chunkSize);
    }

    bool HandleOnDevelopmentTransferEnd(Network::SocketID, Network::Message& message)
    {
        u32 requestID = 0;
        u8 succeeded = 0;
        if (!message.buffer->GetU32(requestID) || !message.buffer->GetU8(succeeded) || succeeded > 1)
            return false;

        Editor::CreatureAIEditorBackend* backend = GetCreatureAIEditorBackend();
        return backend && backend->CompleteTransfer(requestID, succeeded == 1);
    }

    bool HandleOnCreatureAIDevelopmentInfo(Network::SocketID, MetaGen::Shared::Packet::ServerCreatureAIDevelopmentInfoPacket& packet)
    {
        Editor::CreatureAIEditorBackend* backend = GetCreatureAIEditorBackend();
        if (backend)
            backend->HandleInspection(packet);
        return true;
    }

    bool HandleOnAuthChallenge(Network::SocketID socketID, MetaGen::Shared::Packet::ServerAuthChallengePacket& packet)
    {
        entt::registry* gameRegistry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& networkState = gameRegistry->ctx().get<Singletons::NetworkState>();

        if (networkState.authInfo.stage != AuthenticationStage::None)
        {
            NC_LOG_WARNING("Network : Received Auth Challenge while not in None state");
            networkState.authInfo.stage = AuthenticationStage::Failed;
            return false;
        }

        unsigned char response1[crypto_spake_RESPONSE1BYTES];
        i32 result = crypto_spake_step1(&networkState.authInfo.state, response1, packet.challenge.data(), networkState.authInfo.password.c_str(), networkState.authInfo.password.length());
        if (result != 0)
        {
            NC_LOG_WARNING("Network : Failed to process Auth Challenge");
            networkState.authInfo.stage = AuthenticationStage::Failed;
            return false;
        }

        sodium_memzero(networkState.authInfo.password.data(), networkState.authInfo.password.length());

        networkState.authInfo.stage = AuthenticationStage::Step1;
        networkState.authInfo.password.clear();

        MetaGen::Shared::Packet::ClientAuthChallengePacket responsePacket;
        std::memcpy(responsePacket.challenge.data(), response1, crypto_spake_RESPONSE1BYTES);
        Util::Network::SendPacket(networkState, responsePacket);

        return true;
    }
    bool HandleOnAuthProof(Network::SocketID socketID, MetaGen::Shared::Packet::ServerAuthProofPacket& packet)
    {
        entt::registry* gameRegistry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& networkState = gameRegistry->ctx().get<Singletons::NetworkState>();

        if (networkState.authInfo.stage != AuthenticationStage::Step1)
        {
            NC_LOG_WARNING("Network : Received Auth Proof while not in Step1 state");
            networkState.authInfo.stage = AuthenticationStage::Failed;
            return false;
        }

        unsigned char response3[crypto_spake_RESPONSE3BYTES];
        i32 result = crypto_spake_step3(&networkState.authInfo.state, response3, &networkState.authInfo.sharedKeys, networkState.authInfo.username.c_str(), networkState.authInfo.username.length(), "NovusEngine", 11, packet.proof.data());
        if (result != 0)
        {
            NC_LOG_WARNING("Network : Failed to process Auth Proof");
            networkState.authInfo.stage = AuthenticationStage::Failed;
            return false;
        }

        networkState.authInfo.stage = AuthenticationStage::Completed;

        MetaGen::Shared::Packet::ClientAuthProofPacket authProofPacket;
        std::memcpy(authProofPacket.proof.data(), response3, crypto_spake_RESPONSE3BYTES);
        Util::Network::SendPacket(networkState, authProofPacket);

        return true;
    }
    bool HandleOnConnectResult(Network::SocketID socketID, MetaGen::Shared::Packet::ServerConnectResultPacket& packet)
    {
        auto result = static_cast<Network::ConnectResult>(packet.result);

        if (result != Network::ConnectResult::Success)
        {
            NC_LOG_WARNING("Network : Failed to login to character");
            return false;
        }

        NC_LOG_INFO("Network : Logged in to character");
        return true;
    }
    bool HandleOnWorldTransfer(Network::SocketID socketID, MetaGen::Shared::Packet::ServerWorldTransferPacket& packet)
    {
        entt::registry* gameRegistry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& networkState = gameRegistry->ctx().get<Singletons::NetworkState>();

        CleanupNetworkWorldEntities(*gameRegistry);
        networkState.isInWorld = true;

        ServiceLocator::GetLuaManager()->SetDirty();

        MapLoader* mapLoader = ServiceLocator::GetGameRenderer()->GetMapLoader();
        mapLoader->UnloadMapImmediately();

        return true;
    }
    bool HandleOnLoadMap(Network::SocketID socketID, MetaGen::Shared::Packet::ServerLoadMapPacket& packet)
    {
        entt::registry* gameRegistry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        entt::registry* dbRegistry = ServiceLocator::GetEnttRegistries()->dbRegistry;

        auto& networkState = gameRegistry->ctx().get<Singletons::NetworkState>();
        auto& clientDBSingleton = dbRegistry->ctx().get<Singletons::ClientDBSingleton>();

        auto* mapStorage = clientDBSingleton.Get(ClientDBHash::Map);

        if (!mapStorage->Has(packet.mapID))
        {
            NC_LOG_WARNING("Network : Received LoadMap for non existing map ({0})", packet.mapID);
            return false;
        }

        MapLoader* mapLoader = ServiceLocator::GetGameRenderer()->GetMapLoader();
        if (mapLoader->GetCurrentMapID() == packet.mapID)
        {
            NC_LOG_INFO("Network : Received LoadMap for already loaded map ({0})", packet.mapID);
            return false;
        }

        const auto& map = mapStorage->Get<MetaGen::Shared::ClientDB::MapRecord>(packet.mapID);
        const std::string& mapInternalName = mapStorage->GetString(map.internalName);

        u32 internalMapNameHash = StringUtils::fnv1a_32(mapInternalName.c_str(), mapInternalName.length());
        mapLoader->LoadMap(internalMapNameHash);
        networkState.isLoadingMap = true;
        return true;
    }
    bool HandleOnCharacterLogout(Network::SocketID socketID, MetaGen::Shared::Packet::ServerCharacterLogoutPacket& packet)
    {
        entt::registry* gameRegistry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& networkState = gameRegistry->ctx().get<Singletons::NetworkState>();

        CleanupNetworkWorldEntities(*gameRegistry);
        Util::Faction::ResetOwnerState(*gameRegistry);
        networkState.characterListInfo.characterSelected = false;

        ServiceLocator::GetLuaManager()->SetDirty();

        MapLoader* mapLoader = ServiceLocator::GetGameRenderer()->GetMapLoader();
        mapLoader->UnloadMapImmediately();

        return true;
    }
    bool HandleOnPong(Network::SocketID socketID, MetaGen::Shared::Packet::ServerPongPacket& packet)
    {
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& networkState = registry->ctx().get<Singletons::NetworkState>();

        auto currentTime = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        f64 rtt = static_cast<f64>(currentTime) - static_cast<f64>(networkState.pingInfo.lastPingTime);
        u16 ping = static_cast<u16>(rtt / 2.0f);

        networkState.pingInfo.lastPongTime = currentTime;

        u8 pingHistoryCounter = networkState.pingInfo.pingHistoryIndex + 1;
        if (pingHistoryCounter == networkState.pingInfo.pingHistory.size())
            pingHistoryCounter = 0;

        networkState.pingInfo.pingHistorySize = glm::min(static_cast<u8>(networkState.pingInfo.pingHistorySize + 1u), static_cast<u8>(networkState.pingInfo.pingHistory.size()));

        networkState.pingInfo.pingHistoryIndex = pingHistoryCounter;
        networkState.pingInfo.pingHistory[pingHistoryCounter] = ping;

        f32 accumulatedPing = 0.0f;
        for (u16 ping : networkState.pingInfo.pingHistory)
            accumulatedPing += static_cast<f32>(ping);

        accumulatedPing /= networkState.pingInfo.pingHistorySize;
        networkState.pingInfo.ping = static_cast<u16>(glm::round(accumulatedPing));

        return true;
    }
    bool HandleOnServerUpdateStats(Network::SocketID socketID, MetaGen::Shared::Packet::ServerUpdateStatsPacket& packet)
    {
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& networkState = registry->ctx().get<Singletons::NetworkState>();

        networkState.pingInfo.serverUpdateDiff = packet.serverTickTime;

        return true;
    }

    bool HandleOnCheatCommandResult(Network::SocketID socketID, MetaGen::Shared::Packet::ServerCheatCommandResultPacket& packet)
    {
        return true;
    }

    bool ReadBoundedString(Bytebuffer& buffer, std::string& value, size_t maxLength)
    {
        value.clear();
        value.reserve(std::min(buffer.GetActiveSize(), maxLength));
        for (size_t length = 0; length <= maxLength; length++)
        {
            u8 character = 0;
            if (!buffer.GetU8(character))
                return false;
            if (character == 0)
                return true;
            if (length == maxLength)
                return false;

            value.push_back(static_cast<char>(character));
        }

        return false;
    }

    bool HandleOnInteractionSnapshot(Network::SocketID, Network::Message& message)
    {
        constexpr u16 MAX_OPTIONS = 64;
        constexpr size_t MAX_TEXT_LENGTH = 4096;

        MetaGen::Shared::Packet::ServerInteractionSnapshotPacket packet;
        if (!message.buffer->GetU64(packet.sessionID) || !message.buffer->GetU32(packet.revision) || !message.buffer->Deserialize(packet.sourceGUID) || !message.buffer->GetU8(packet.surfaceType) ||
            !ReadBoundedString(*message.buffer, packet.greeting, MAX_TEXT_LENGTH) || !message.buffer->GetU16(packet.optionCount) ||
            packet.sessionID == 0 || packet.revision == 0 || !packet.sourceGUID.IsValid() || packet.surfaceType >= static_cast<u8>(MetaGen::Shared::Interaction::InteractionSurfaceTypeEnum::Count) ||
            packet.optionCount > MAX_OPTIONS || (packet.greeting.empty() && packet.optionCount == 0))
        {
            return false;
        }

        Singletons::InteractionSessionState session{
            .id = packet.sessionID,
            .revision = packet.revision,
            .sourceGUID = packet.sourceGUID,
            .surfaceType = static_cast<MetaGen::Shared::Interaction::InteractionSurfaceTypeEnum>(packet.surfaceType),
            .greeting = std::move(packet.greeting)
        };

        session.options.reserve(packet.optionCount);
        for (u16 i = 0; i < packet.optionCount; i++)
        {
            Singletons::InteractionOptionState option;
            u8 enabled = 0;
            if (!message.buffer->GetU64(option.token) || !message.buffer->GetU16(option.icon) || !message.buffer->GetU8(enabled) || !ReadBoundedString(*message.buffer, option.text, MAX_TEXT_LENGTH) || !ReadBoundedString(*message.buffer, option.disabledReason, MAX_TEXT_LENGTH))
            {
                return false;
            }
            if (option.token == 0 || enabled > 1 || option.text.empty())
                return false;

            option.enabled = enabled == 1;
            session.options.push_back(std::move(option));
        }

        if (message.buffer->GetActiveSize() != 0)
            return false;

        auto& networkState = ServiceLocator::GetEnttRegistries()->gameRegistry->ctx().get<Singletons::NetworkState>();
        networkState.interactionState.activeSession = std::move(session);
        networkState.interactionState.lastClosedSessionID = 0;
        networkState.interactionState.lastCloseReason = MetaGen::Shared::Interaction::InteractionCloseReasonEnum::Count;
        networkState.interactionState.lastResultSessionID = 0;
        networkState.interactionState.lastResultRevision = 0;
        networkState.interactionState.lastResult = MetaGen::Shared::Interaction::InteractionResultEnum::Count;
        networkState.interactionState.Touch();
        return true;
    }

    bool HandleOnInteractionClose(Network::SocketID, MetaGen::Shared::Packet::ServerInteractionClosePacket& packet)
    {
        if (packet.sessionID == 0 || packet.reason >= static_cast<u8>(MetaGen::Shared::Interaction::InteractionCloseReasonEnum::Count))
            return false;

        auto& interactionState = ServiceLocator::GetEnttRegistries()->gameRegistry->ctx().get<Singletons::NetworkState>().interactionState;
        if (interactionState.activeSession && interactionState.activeSession->id == packet.sessionID)
            interactionState.activeSession.reset();
        interactionState.lastClosedSessionID = packet.sessionID;
        interactionState.lastCloseReason = static_cast<MetaGen::Shared::Interaction::InteractionCloseReasonEnum>(packet.reason);
        interactionState.Touch();
        return true;
    }

    bool HandleOnInteractionResult(Network::SocketID, MetaGen::Shared::Packet::ServerInteractionResultPacket& packet)
    {
        const bool isOpenRejection = packet.sessionID == 0 && packet.revision == 0;
        const bool isSessionResult = packet.sessionID != 0 && packet.revision != 0;
        if ((!isOpenRejection && !isSessionResult) || packet.result >= static_cast<u8>(MetaGen::Shared::Interaction::InteractionResultEnum::Count))
            return false;
        if (isOpenRejection && packet.result != static_cast<u8>(MetaGen::Shared::Interaction::InteractionResultEnum::Unavailable))
            return false;

        auto& interactionState = ServiceLocator::GetEnttRegistries()->gameRegistry->ctx().get<Singletons::NetworkState>().interactionState;
        interactionState.lastResultSessionID = packet.sessionID;
        interactionState.lastResultRevision = packet.revision;
        interactionState.lastResult = static_cast<MetaGen::Shared::Interaction::InteractionResultEnum>(packet.result);
        interactionState.Touch();
        return true;
    }

    bool HandleOnUnitInteractionUpdate(Network::SocketID, MetaGen::Shared::Packet::ServerUnitInteractionUpdatePacket& packet)
    {
        constexpr u8 VALID_CAPABILITIES = static_cast<u8>(MetaGen::Shared::Interaction::InteractionCapabilityMaskEnum::All);
        if (!packet.guid.IsValid() || (packet.capabilities & static_cast<u8>(~VALID_CAPABILITIES)) != 0)
            return false;

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& networkState = registry->ctx().get<Singletons::NetworkState>();
        entt::entity entity = entt::null;
        if (!Util::Network::GetEntityIDFromObjectGUID(networkState, packet.guid, entity))
        {
            NC_LOG_WARNING("Network : Received ServerUnitInteractionUpdate for unknown entity ({0})", packet.guid.ToString());
            return true;
        }

        if (packet.capabilities == 0)
        {
            registry->remove<Components::InteractionCapabilities>(entity);
        }
        else
        {
            registry->emplace_or_replace<Components::InteractionCapabilities>(entity, Components::InteractionCapabilities{ .value = static_cast<MetaGen::Shared::Interaction::InteractionCapabilityMaskEnum>(packet.capabilities) });
        }

        return true;
    }

    bool HandleOnUnitFactionUpdate(Network::SocketID socketID, MetaGen::Shared::Packet::ServerUnitFactionUpdatePacket& packet)
    {
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& networkState = registry->ctx().get<Singletons::NetworkState>();
        entt::entity entity = entt::null;
        if (!Util::Network::GetEntityIDFromObjectGUID(networkState, packet.guid, entity))
        {
            NC_LOG_WARNING("Network : Received ServerUnitFactionUpdate for unknown entity ({0})", packet.guid.ToString());
            return true;
        }

        Util::Faction::ApplyUnitUpdate(*registry, entity, packet.factionID, packet.playerReactionBounds);
        return true;
    }

    bool HandleOnReputationUpdate(Network::SocketID socketID, MetaGen::Shared::Packet::ServerReputationUpdatePacket& packet)
    {
        if (packet.isPresent > 1)
        {
            NC_LOG_WARNING("Network : Received ServerReputationUpdate with invalid presence value ({0})", packet.isPresent);
            return false;
        }

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        Util::Faction::ApplyReputationUpdate(*registry, packet.factionID, packet.value, packet.flags, packet.isPresent == 1);
        return true;
    }

    bool HandleOnFactionPerceptionOverrideUpdate(Network::SocketID socketID, MetaGen::Shared::Packet::ServerFactionPerceptionOverrideUpdatePacket& packet)
    {
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        Util::Faction::ApplyPerceptionUpdate(*registry, packet.factionID, packet.activeFields, packet.effectiveStandingValue, packet.effectiveReaction);
        return true;
    }

    bool HandleOnUnitAdd(Network::SocketID socketID, MetaGen::Shared::Packet::ServerUnitAddPacket& packet)
    {
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& networkState = registry->ctx().get<Singletons::NetworkState>();

        if (Util::Network::IsObjectGUIDKnown(networkState, packet.guid))
        {
            NC_LOG_WARNING("Network : Received ServerUnitAdd for already existing entity ({0})", packet.guid.ToString());
            return true;
        }

        entt::entity newEntity = registry->create();
        registry->emplace<Components::AABB>(newEntity);
        registry->emplace<Components::WorldAABB>(newEntity);
        registry->emplace<Components::Transform>(newEntity);
        registry->emplace<Components::Name>(newEntity);
        registry->emplace<Components::Model>(newEntity);
        registry->emplace<Components::UnitAuraInfo>(newEntity);
        registry->emplace<Components::UnitCustomization>(newEntity);
        registry->emplace<Components::UnitEquipment>(newEntity);
        registry->emplace<Components::UnitMovementOverTime>(newEntity);
        registry->emplace<Components::UnitPowersComponent>(newEntity);
        registry->emplace<Components::UnitResistancesComponent>(newEntity);
        registry->emplace<Components::UnitStatsComponent>(newEntity);
        registry->emplace<Components::AttachmentData>(newEntity);
        auto& displayInfo = registry->emplace<Components::DisplayInfo>(newEntity);
        displayInfo.displayID = 0;

        auto& unit = registry->emplace<Components::Unit>(newEntity);
        unit.networkID = packet.guid;
        unit.name = packet.name;
        unit.targetEntity = entt::null;
        unit.unitClass = static_cast<GameDefine::UnitClass>(packet.unitClass);
        unit.scale = packet.scale.x;

        if (unit.networkID.GetType() == ObjectGUID::Type::Player)
            registry->emplace_or_replace<Components::PlayerTag>(newEntity);

        auto& movementInfo = registry->emplace<Components::MovementInfo>(newEntity);
        movementInfo.pitch = packet.pitchYaw.x;
        movementInfo.yaw = packet.pitchYaw.y;
        movementInfo.movementFlags.grounded = true;

        auto& objectFields = registry->emplace<Components::ObjectFields>(newEntity);
        auto& unitFields = registry->emplace<Components::UnitFields>(newEntity);

        objectFields.fields.SetField(MetaGen::Shared::NetField::ObjectNetFieldEnum::ObjectGUIDLow, packet.guid);
        objectFields.fields.SetField(MetaGen::Shared::NetField::ObjectNetFieldEnum::Scale, 1.0f);

        TransformSystem& transformSystem = TransformSystem::Get(*registry);

        quat rotation = quat(glm::vec3(packet.pitchYaw.x, packet.pitchYaw.y, 0.0f));
        transformSystem.SetWorldPosition(newEntity, packet.position);
        transformSystem.SetWorldRotation(newEntity, rotation);
        transformSystem.SetLocalScale(newEntity, packet.scale);

        networkState.networkIDToEntity[packet.guid] = newEntity;
        networkState.entityToNetworkID[newEntity] = packet.guid;

        Util::Faction::AttachUnit(*registry, newEntity);

        Scripting::Zenith* zenith = Scripting::Util::Zenith::GetGlobal();
        zenith->CallEvent(MetaGen::Game::Lua::UnitEvent::Add, MetaGen::Game::Lua::UnitEventDataAdd{ .unitID = entt::to_integral(newEntity) });

        return true;
    }
    bool HandleOnUnitRemove(Network::SocketID socketID, MetaGen::Shared::Packet::ServerUnitRemovePacket& packet)
    {
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& networkState = registry->ctx().get<Singletons::NetworkState>();

        entt::entity entity;
        if (!Util::Network::GetEntityIDFromObjectGUID(networkState, packet.guid, entity))
        {
            NC_LOG_WARNING("Network : Received ServerUnitRemove for unknown entity ({0})", packet.guid.ToString());
            return true;
        }

        if (auto* attachmentData = registry->try_get<Components::AttachmentData>(entity))
        {
            for (auto& pair : attachmentData->attachmentToInstance)
            {
                ::Util::Unit::RemoveItemFromAttachment(*registry, entity, pair.first);
            }
        }

        if (auto* model = registry->try_get<Components::Model>(entity))
        {
            ModelLoader* modelLoader = ServiceLocator::GetGameRenderer()->GetModelLoader();
            modelLoader->UnloadModelForEntity(entity, *model);

            registry->remove<Components::AnimationData>(entity);
        }

        networkState.networkIDToEntity.erase(packet.guid);
        networkState.entityToNetworkID.erase(entity);
        networkState.networkVisTree->Remove(packet.guid);

        Util::Faction::DetachUnit(*registry, entity);
        registry->destroy(entity);

        Scripting::Zenith* zenith = Scripting::Util::Zenith::GetGlobal();
        zenith->CallEvent(MetaGen::Game::Lua::UnitEvent::Remove, MetaGen::Game::Lua::UnitEventDataRemove{ .unitID = entt::to_integral(entity) });

        return true;
    }

    bool HandleOnUnitEquippedItemUpdate(Network::SocketID socketID, MetaGen::Shared::Packet::ServerUnitEquippedItemUpdatePacket& packet)
    {
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& networkState = registry->ctx().get<Singletons::NetworkState>();

        entt::entity entity;
        if (!Util::Network::GetEntityIDFromObjectGUID(networkState, packet.guid, entity))
        {
            NC_LOG_WARNING("Network : Received Visual Item Update for non existing entity ({0})", packet.guid.ToString());
            return true;
        }

        if (!registry->valid(entity))
        {
            NC_LOG_WARNING("Network : Received Visual Item Update for non existing entity ({0})", packet.guid.ToString());
            return true;
        }

        auto& unitEquipment = registry->get<Components::UnitEquipment>(entity);
        if (packet.slot >= unitEquipment.equipmentSlotToVisualItemID.size())
        {
            NC_LOG_WARNING("Network : Received Visual Item Update for invalid slot ({0})", packet.slot);
            return true;
        }

        unitEquipment.equipmentSlotToItemID[packet.slot] = packet.itemID;
        unitEquipment.dirtyItemIDSlots.insert(static_cast<MetaGen::Shared::Unit::ItemEquipSlotEnum>(packet.slot));
        registry->emplace_or_replace<Components::UnitEquipmentDirty>(entity);

        if (packet.slot == static_cast<u8>(MetaGen::Shared::Unit::ItemEquipSlotEnum::MainHand))
        {
            const auto* unit = registry->try_get<Components::Unit>(entity);
            if (unit && unit->isAutoAttacking)
                ::Util::Unit::SetAutoAttackVisualState(*registry, entity, true);
        }

        return true;
    }
    bool HandleOnUnitVisualItemUpdate(Network::SocketID socketID, MetaGen::Shared::Packet::ServerUnitVisualItemUpdatePacket& packet)
    {
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& networkState = registry->ctx().get<Singletons::NetworkState>();

        entt::entity entity;
        if (!Util::Network::GetEntityIDFromObjectGUID(networkState, packet.guid, entity))
        {
            NC_LOG_WARNING("Network : Received Visual Item Update for non existing entity ({0})", packet.guid.ToString());
            return true;
        }

        if (!registry->valid(entity))
        {
            NC_LOG_WARNING("Network : Received Visual Item Update for non existing entity ({0})", packet.guid.ToString());
            return true;
        }

        auto& unitEquipment = registry->get<Components::UnitEquipment>(entity);
        if (packet.slot >= unitEquipment.equipmentSlotToVisualItemID.size())
        {
            NC_LOG_WARNING("Network : Received Visual Item Update for invalid slot ({0})", packet.slot);
            return true;
        }

        unitEquipment.equipmentSlotToVisualItemID[packet.slot] = packet.itemID;
        unitEquipment.dirtyVisualItemIDSlots.insert(static_cast<MetaGen::Shared::Unit::ItemEquipSlotEnum>(packet.slot));
        registry->emplace_or_replace<Components::UnitVisualEquipmentDirty>(entity);

        if (packet.slot == static_cast<u8>(MetaGen::Shared::Unit::ItemEquipSlotEnum::MainHand))
        {
            const auto* unit = registry->try_get<Components::Unit>(entity);
            if (unit && unit->isAutoAttacking)
                ::Util::Unit::SetAutoAttackVisualState(*registry, entity, true);
        }

        return true;
    }

    bool HandleOnUnitPowerUpdate(Network::SocketID socketID, MetaGen::Shared::Packet::ServerUnitPowerUpdatePacket& packet)
    {
        auto powerType = static_cast<MetaGen::Shared::Unit::PowerTypeEnum>(packet.kind);
        if (powerType <= MetaGen::Shared::Unit::PowerTypeEnum::Invalid || powerType >= MetaGen::Shared::Unit::PowerTypeEnum::Count)
        {
            NC_LOG_WARNING("Network : Received Power Update for unknown PowerType ({0})", packet.kind);
            return true;
        }

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& networkState = registry->ctx().get<Singletons::NetworkState>();

        entt::entity entity;
        if (!Util::Network::GetEntityIDFromObjectGUID(networkState, packet.guid, entity))
        {
            NC_LOG_WARNING("Network : Received Power Update for non existing entity ({0})", packet.guid.ToString());
            return true;
        }

        if (!registry->valid(entity))
        {
            NC_LOG_WARNING("Network : Received Power Update for non existing entity ({0})", packet.guid.ToString());
            return true;
        }

        auto& unitPowersComponent = registry->get<Components::UnitPowersComponent>(entity);
        if (!::Util::Unit::SetPower(unitPowersComponent, powerType, packet.base, packet.current, packet.max))
        {
            ::Util::Unit::AddPower(unitPowersComponent, powerType, packet.base, packet.current, packet.max);
        }

        Scripting::Zenith* zenith = Scripting::Util::Zenith::GetGlobal();
        zenith->CallEvent(MetaGen::Game::Lua::UnitEvent::PowerUpdate, MetaGen::Game::Lua::UnitEventDataPowerUpdate{ .unitID = entt::to_integral(entity), .powerType = packet.kind, .base = packet.base, .current = packet.current, .max = packet.max });

        return true;
    }
    bool HandleOnUnitResistanceUpdate(Network::SocketID socketID, MetaGen::Shared::Packet::ServerUnitResistanceUpdatePacket& packet)
    {
        auto resistanceType = static_cast<MetaGen::Shared::Unit::ResistanceTypeEnum>(packet.kind);
        if (resistanceType <= MetaGen::Shared::Unit::ResistanceTypeEnum::Invalid || resistanceType >= MetaGen::Shared::Unit::ResistanceTypeEnum::Count)
        {
            NC_LOG_WARNING("Network : Received Resistance Update for unknown ResistanceType ({0})", packet.kind);
            return true;
        }

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& characterSingleton = registry->ctx().get<Singletons::CharacterSingleton>();

        entt::entity entity = characterSingleton.moverEntity;
        if (!registry->valid(entity))
        {
            NC_LOG_WARNING("Network : Received Resistance Update while no mover entity is active");
            return true;
        }

        auto& unitResistancesComponent = registry->get<Components::UnitResistancesComponent>(entity);
        if (!::Util::Unit::SetResistance(unitResistancesComponent, resistanceType, packet.base, packet.current, packet.max))
        {
            ::Util::Unit::AddResistance(unitResistancesComponent, resistanceType, packet.base, packet.current, packet.max);
        }

        Scripting::Zenith* zenith = Scripting::Util::Zenith::GetGlobal();
        zenith->CallEvent(MetaGen::Game::Lua::UnitEvent::ResistanceUpdate, MetaGen::Game::Lua::UnitEventDataResistanceUpdate{ .unitID = entt::to_integral(entity), .resistanceType = packet.kind, .base = packet.base, .current = packet.current, .max = packet.max });

        return true;
    }
    bool HandleOnUnitStatUpdate(Network::SocketID socketID, MetaGen::Shared::Packet::ServerUnitStatUpdatePacket& packet)
    {
        auto statType = static_cast<MetaGen::Shared::Unit::StatTypeEnum>(packet.kind);
        if (statType <= MetaGen::Shared::Unit::StatTypeEnum::Invalid || statType >= MetaGen::Shared::Unit::StatTypeEnum::Count)
        {
            NC_LOG_WARNING("Network : Received Stat Update for unknown StatType ({0})", packet.kind);
            return true;
        }

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& characterSingleton = registry->ctx().get<Singletons::CharacterSingleton>();

        entt::entity entity = characterSingleton.moverEntity;
        if (!registry->valid(entity))
        {
            NC_LOG_WARNING("Network : Received Stat Update while no mover entity is active");
            return true;
        }

        auto& unitStatsComponent = registry->get<Components::UnitStatsComponent>(entity);
        if (!::Util::Unit::SetStat(unitStatsComponent, statType, packet.base, packet.current))
        {
            ::Util::Unit::AddStat(unitStatsComponent, statType, packet.base, packet.current);
        }

        Scripting::Zenith* zenith = Scripting::Util::Zenith::GetGlobal();
        zenith->CallEvent(MetaGen::Game::Lua::UnitEvent::StatUpdate, MetaGen::Game::Lua::UnitEventDataStatUpdate{ .unitID = entt::to_integral(entity), .statType = packet.kind, .base = packet.base, .current = packet.current });
        return true;
    }

    bool HandleOnUnitTargetUpdate(Network::SocketID socketID, MetaGen::Shared::Packet::ServerUnitTargetUpdatePacket& packet)
    {
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& networkState = registry->ctx().get<Singletons::NetworkState>();

        entt::entity entity;
        if (!Util::Network::GetEntityIDFromObjectGUID(networkState, packet.guid, entity))
        {
            NC_LOG_WARNING("Network : Received Target Update for non existing entity ({0})", packet.guid.ToString());
            return true;
        }

        entt::entity targetEntity = entt::null;
        if (packet.targetGUID.IsValid() && !Util::Network::GetEntityIDFromObjectGUID(networkState, packet.targetGUID, targetEntity))
        {
            NC_LOG_WARNING("Network : Received Target Update for non existing target entity ({0})", packet.targetGUID.ToString());
            return true;
        }

        auto& unit = registry->get<Components::Unit>(entity);
        unit.targetEntity = targetEntity;

        Scripting::Zenith* zenith = Scripting::Util::Zenith::GetGlobal();
        zenith->CallEvent(MetaGen::Game::Lua::UnitEvent::TargetChanged, MetaGen::Game::Lua::UnitEventDataTargetChanged{ .unitID = entt::to_integral(entity), .targetID = entt::to_integral(targetEntity) });

        return true;
    }
    bool HandleOnUnitCastSpell(Network::SocketID socketID, MetaGen::Shared::Packet::ServerUnitCastSpellPacket& packet)
    {
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& networkState = registry->ctx().get<Singletons::NetworkState>();

        entt::entity entity;
        if (!Util::Network::GetEntityIDFromObjectGUID(networkState, packet.guid, entity))
        {
            NC_LOG_WARNING("Network : Received Cast Spell for non existing entity ({0})", packet.guid.ToString());
            return true;
        }

        if (!registry->valid(entity))
        {
            NC_LOG_WARNING("Network : Received Cast Spell for non existing entity ({0})", packet.guid.ToString());
            return true;
        }

        auto* unit = registry->try_get<Components::Unit>(entity);
        if (!unit)
        {
            NC_LOG_WARNING("Network : Received Cast Spell for non existing entity ({0})", packet.guid.ToString());
            return true;
        }

        const AutoAttackWeaponSlot weaponSlot = GetAutoAttackWeaponSlot(packet.spellID);
        if (weaponSlot != AutoAttackWeaponSlot::None)
        {
            QueueUnitAttackAnimation(*registry, entity, weaponSlot);
        }
        else
        {
            auto& castInfo = registry->emplace_or_replace<Components::CastInfo>(entity);

            castInfo.target = unit->targetEntity;
            castInfo.castTime = packet.castTime;
            castInfo.timeToCast = packet.timeToCast;
        }

        return true;
    }
    bool HandleOnUnitAttack(Network::SocketID socketID, MetaGen::Shared::Packet::ServerUnitAttackPacket& packet)
    {
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& networkState = registry->ctx().get<Singletons::NetworkState>();

        entt::entity entity;
        if (!Util::Network::GetEntityIDFromObjectGUID(networkState, packet.guid, entity) || !registry->valid(entity))
        {
            NC_LOG_WARNING("Network : Received Unit Attack for non existing entity ({0})", packet.guid.ToString());
            return true;
        }

        const auto weaponSlot = static_cast<AutoAttackWeaponSlot>(packet.weaponSlot);
        if (weaponSlot != AutoAttackWeaponSlot::MainHand && weaponSlot != AutoAttackWeaponSlot::OffHand)
        {
            NC_LOG_WARNING("Network : Received Unit Attack with invalid weapon slot ({0})", packet.weaponSlot);
            return true;
        }

        QueueUnitAttackAnimation(*registry, entity, weaponSlot);
        return true;
    }
    bool HandleOnUnitAutoAttackState(Network::SocketID socketID, MetaGen::Shared::Packet::ServerUnitAutoAttackStatePacket& packet)
    {
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& networkState = registry->ctx().get<Singletons::NetworkState>();

        entt::entity entity;
        if (!Util::Network::GetEntityIDFromObjectGUID(networkState, packet.guid, entity) || !registry->valid(entity))
        {
            NC_LOG_WARNING("Network : Received Auto Attack State for non existing entity ({0})", packet.guid.ToString());
            return true;
        }

        if (!registry->all_of<Components::Unit>(entity))
            return true;

        ::Util::Unit::SetAutoAttackVisualState(*registry, entity, packet.enabled != 0);

        return true;
    }
    bool HandleOnUnitSetMover(Network::SocketID socketID, MetaGen::Shared::Packet::ServerUnitSetMoverPacket& packet)
    {
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& networkState = registry->ctx().get<Singletons::NetworkState>();

        entt::entity entity;
        if (!Util::Network::GetEntityIDFromObjectGUID(networkState, packet.guid, entity))
        {
            NC_LOG_WARNING("Network : Received ServerUnitSetMover for non-existent entity ({0})", packet.guid.ToString());
            return true;
        }

        auto& characterSingleton = registry->ctx().get<Singletons::CharacterSingleton>();
        characterSingleton.moverEntity = entity;

        Util::Faction::SetLocalPlayer(*registry, entity);

        InitActiveCharacterController(*registry, false);
        OrbitalCamera::ResetForNewWorld(*registry);

        Scripting::Zenith* zenith = Scripting::Util::Zenith::GetGlobal();
        zenith->CallEvent(MetaGen::Game::Lua::GameEvent::LocalMoverChanged, MetaGen::Game::Lua::GameEventDataLocalMoverChanged{ .moverID = entt::to_integral(entity) });

        return true;
    }
    bool HandleOnUnitMove(Network::SocketID socketID, MetaGen::Shared::Packet::ServerUnitMovePacket& packet)
    {
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& networkState = registry->ctx().get<Singletons::NetworkState>();

        entt::entity entity;
        if (!Util::Network::GetEntityIDFromObjectGUID(networkState, packet.guid, entity))
        {
            NC_LOG_WARNING("Network : Received Entity Move for non existing entity ({0})", packet.guid.ToString());
            return true;
        }

        auto& unitMovementOverTime = registry->get<Components::UnitMovementOverTime>(entity);
        auto& transform = registry->get<Components::Transform>(entity);
        auto& movementInfo = registry->get<Components::MovementInfo>(entity);
        auto& characterSingleton = registry->ctx().get<Singletons::CharacterSingleton>();

        const bool applyPositionDirectly = CVAR_NetworkDirectRemoteUnitPosition.Get() != 0 && entity != characterSingleton.moverEntity;
        const f64 snapshotTime = std::chrono::duration<f64>(std::chrono::steady_clock::now().time_since_epoch()).count();
        f32 snapshotSpacing = unitMovementOverTime.hasSnapshot ? static_cast<f32>(snapshotTime - unitMovementOverTime.lastSnapshotTime) : 0.1f;
        snapshotSpacing = glm::clamp(snapshotSpacing, 0.05f, 0.25f);

        unitMovementOverTime.startPos = transform.GetWorldPosition();
        unitMovementOverTime.endPos = packet.position;
        unitMovementOverTime.elapsed = 0.0f;
        unitMovementOverTime.duration = snapshotSpacing;
        unitMovementOverTime.lastSnapshotTime = snapshotTime;
        unitMovementOverTime.hasSnapshot = true;

        if (applyPositionDirectly)
        {
            unitMovementOverTime.startPos = packet.position;
            unitMovementOverTime.elapsed = unitMovementOverTime.duration;
        }

        movementInfo.pitch = packet.pitchYaw.x;
        movementInfo.yaw = packet.pitchYaw.y;
        movementInfo.movementFlags = *reinterpret_cast<Components::MovementFlags*>(&packet.movementFlags);

        bool isGrounded = movementInfo.movementFlags.grounded;
        if (isGrounded)
        {
            if (movementInfo.movementFlags.jumping || movementInfo.jumpState != Components::JumpState::None)
            {
                movementInfo.jumpState = Components::JumpState::None;
            }
        }
        else
        {
            bool isInJump = movementInfo.movementFlags.jumping;
            if (isInJump)
            {
                if (movementInfo.jumpState == Components::JumpState::None)
                    movementInfo.jumpState = Components::JumpState::Begin;
            }
        }

        movementInfo.verticalVelocity = packet.verticalVelocity;

        TransformSystem& transformSystem = TransformSystem::Get(*registry);

        quat rotation = quat(vec3(packet.pitchYaw.x, packet.pitchYaw.y, 0.0f));
        transformSystem.SetWorldRotation(entity, rotation);

        if (applyPositionDirectly)
        {
            transformSystem.SetWorldPosition(entity, packet.position);

            auto& unit = registry->get<Components::Unit>(entity);
            if (unit.bodyID != std::numeric_limits<u32>().max())
            {
                auto& joltState = registry->ctx().get<Singletons::JoltState>();
                JPH::BodyID bodyID = JPH::BodyID(unit.bodyID);
                joltState.physicsSystem.GetBodyInterface().SetPositionAndRotation(bodyID, JPH::Vec3(packet.position.x, packet.position.y, packet.position.z), JPH::Quat(rotation.x, rotation.y, rotation.z, rotation.w), JPH::EActivation::DontActivate);
            }

        }

        return true;
    }
    bool HandleOnUnitMoveStop(Network::SocketID socketID, MetaGen::Shared::Packet::SharedUnitMoveStopPacket& packet)
    {
        return true;
    }
    bool HandleOnUnitTeleport(Network::SocketID socketID, MetaGen::Shared::Packet::ServerUnitTeleportPacket& packet)
    {
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& networkState = registry->ctx().get<Singletons::NetworkState>();

        entt::entity entity;
        if (!Util::Network::GetEntityIDFromObjectGUID(networkState, packet.guid, entity))
        {
            NC_LOG_WARNING("Network : Received Entity Teleport for non existing entity ({0})", packet.guid.ToString());
            return true;
        }

        auto& characterSingleton = registry->ctx().get<Singletons::CharacterSingleton>();

        auto& movementInfo = registry->get<Components::MovementInfo>(entity);
        movementInfo.yaw = packet.orientation;
        movementInfo.verticalVelocity = 0.0f;

        auto& transformSystem = TransformSystem::Get(*registry);
        auto& transform = registry->get<Components::Transform>(entity);

        auto rotation = quat(vec3(movementInfo.pitch, movementInfo.yaw, 0.0f));
        auto joltRotation = JPH::Quat(rotation.x, rotation.y, rotation.z, rotation.w);

        bool isLocalMover = entity == characterSingleton.moverEntity;
        if (isLocalMover)
        {
            transformSystem.SetWorldPosition(characterSingleton.controllerEntity, packet.position);

            characterSingleton.character->SetPosition(JPH::Vec3(packet.position.x, packet.position.y, packet.position.z));
            characterSingleton.character->SetRotation(joltRotation);
            characterSingleton.character->SetLinearVelocity(JPH::Vec3::sZero());

            auto& characterState = registry->ctx().get<Singletons::CharacterControllerSingleton>();
            characterState.groundMovementMode = Singletons::CharacterGroundMovementMode::Fall;
            characterState.unsupportedState.elapsedFallTime = 0.0f;
            characterState.unsupportedState.elapsedFallDistance = 0.0f;

            auto& orbitalCameraSettings = registry->ctx().get<Singletons::OrbitalCameraSettings>();
            if (orbitalCameraSettings.entity != entt::null)
            {
                auto& camera = registry->get<Components::Camera>(orbitalCameraSettings.entity);
                camera.yaw = glm::degrees(movementInfo.yaw);
            }
        }
        else
        {
            transformSystem.SetWorldPosition(entity, packet.position);
            transformSystem.SetWorldRotation(entity, rotation);

            auto& unit = registry->get<Components::Unit>(entity);
            if (unit.bodyID != std::numeric_limits<u32>().max())
            {
                auto& joltState = registry->ctx().get<Singletons::JoltState>();

                JPH::BodyID bodyID = JPH::BodyID(unit.bodyID);
                auto& bodyInterface = joltState.physicsSystem.GetBodyInterface();

                if (bodyInterface.IsActive(bodyID))
                {
                    bodyInterface.SetPositionAndRotation(bodyID, JPH::Vec3(packet.position.x, packet.position.y, packet.position.z), joltRotation, JPH::EActivation::DontActivate);
                    bodyInterface.SetLinearVelocity(bodyID, JPH::Vec3::sZero());
                }
            }
        }

        if (auto* unitMovementOverTime = registry->try_get<Components::UnitMovementOverTime>(entity))
        {
            unitMovementOverTime->startPos = packet.position;
            unitMovementOverTime->endPos = packet.position;
            unitMovementOverTime->elapsed = unitMovementOverTime->duration;
            unitMovementOverTime->hasRenderedPosition = false;
        }

        return true;
    }

    bool HandleOnItemAdd(Network::SocketID socketID, MetaGen::Shared::Packet::ServerItemAddPacket& packet)
    {
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& networkState = registry->ctx().get<Singletons::NetworkState>();

        if (Util::Network::IsObjectGUIDKnown(networkState, packet.guid))
        {
            NC_LOG_WARNING("Network : Received ServerItemAdd for already existing item ({0})", packet.guid.ToString());
            return true;
        }

        entt::entity newItemEntity = registry->create();

        auto& item = registry->emplace<Components::Item>(newItemEntity);
        item.guid = packet.guid;
        item.itemID = packet.itemID;
        item.count = packet.count;
        item.durability = packet.durability;

        auto& name = registry->emplace<Components::Name>(newItemEntity);

        std::string itemName = packet.guid.ToString();
        name.name = itemName;
        name.fullName = itemName;
        name.nameHash = StringUtils::fnv1a_32(itemName.c_str(), itemName.size());

        networkState.networkIDToEntity[packet.guid] = newItemEntity;
        networkState.entityToNetworkID[newItemEntity] = packet.guid;

        return true;
    }

    bool HandleOnContainerAdd(Network::SocketID socketID, MetaGen::Shared::Packet::ServerContainerAddPacket& packet)
    {
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& networkState = registry->ctx().get<Singletons::NetworkState>();

        if (packet.index >= 6)
        {
            NC_LOG_WARNING("Network : Received Container Add for invalid container index ({0})", packet.index);
            return true;
        }

        if (Util::Network::IsObjectGUIDKnown(networkState, packet.guid))
        {
            NC_LOG_WARNING("Network : Received ServerContainerAdd for already existing container ({0})", packet.guid.ToString());
            return true;
        }

        Components::Container container =
        {
            .itemID = packet.itemID,
            .numSlots = packet.numSlots,
            .numFreeSlots = packet.numFreeSlots,
        };
        container.items.resize(packet.numSlots);

        entt::entity newContainerEntity = registry->create();

        auto& characterSingleton = registry->ctx().get<Singletons::CharacterSingleton>();

        if (packet.index == 0)
        {
            characterSingleton.baseContainerEntity = newContainerEntity;

            auto& name = registry->emplace<Components::Name>(newContainerEntity);
            std::string containerName = "Player Base Container";
            name.name = containerName;
            name.fullName = containerName;
            name.nameHash = StringUtils::fnv1a_32(containerName.c_str(), containerName.size());
        }
        else
        {
            auto& name = registry->emplace<Components::Name>(newContainerEntity);
            std::string containerName = packet.guid.ToString();
            name.name = containerName;
            name.fullName = containerName;
            name.nameHash = StringUtils::fnv1a_32(containerName.c_str(), containerName.size());

            auto& itemComp = registry->emplace<Components::Item>(newContainerEntity);
            itemComp.guid = packet.guid;
            itemComp.itemID = packet.itemID;
            itemComp.count = 1;
            itemComp.durability = packet.numSlots;

            networkState.networkIDToEntity[packet.guid] = newContainerEntity;
            networkState.entityToNetworkID[newContainerEntity] = packet.guid;
        }

        registry->emplace<Components::Container>(newContainerEntity, container);
        characterSingleton.containers[packet.index] = packet.guid;

        Scripting::Zenith* zenith = Scripting::Util::Zenith::GetGlobal();
        zenith->CallEvent(MetaGen::Game::Lua::ContainerEvent::Add, MetaGen::Game::Lua::ContainerEventDataAdd{ .index = packet.index + 1u, .numSlots = container.numSlots, .itemID = container.itemID });
        return true;
    }
    bool HandleOnContainerAddToSlot(Network::SocketID socketID, MetaGen::Shared::Packet::ServerContainerAddToSlotPacket& packet)
    {
        if (packet.index >= 6)
        {
            NC_LOG_WARNING("Network : Received Container Add To Slot for invalid container index ({0})", packet.index);
            return true;
        }

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& characterSingleton = registry->ctx().get<Singletons::CharacterSingleton>();
        auto& networkState = registry->ctx().get<Singletons::NetworkState>();

        entt::entity itemEntity;
        if (!Util::Network::GetEntityIDFromObjectGUID(networkState, packet.guid, itemEntity))
        {
            NC_LOG_WARNING("Network : Received Container Add To Slot for non existing item ({0})", packet.guid.ToString());
            return true;
        }

        Components::Container* containerPtr = nullptr;
        if (packet.index == 0)
        {
            if (!registry->valid(characterSingleton.baseContainerEntity))
            {
                NC_LOG_WARNING("Network : Received Container Add To Slot for non existing base container");
                return true;
            }

            auto& baseContainer = registry->get<Components::Container>(characterSingleton.baseContainerEntity);
            containerPtr = &baseContainer;
        }
        else
        {
            if (!characterSingleton.containers[packet.index].IsValid())
            {
                NC_LOG_WARNING("Network : Received Container Add To Slot for non existing container ({0})", packet.index);
                return true;
            }

            ObjectGUID containerNetworkID = characterSingleton.containers[packet.index];

            entt::entity containerEntity;
            if (!Util::Network::GetEntityIDFromObjectGUID(networkState, containerNetworkID, containerEntity))
            {
                NC_LOG_WARNING("Network : Received Container Add To Slot for non existing container entity ({0})", containerNetworkID.ToString());
                return true;
            }

            if (!registry->valid(containerEntity))
            {
                NC_LOG_WARNING("Network : Received Container Add To Slot for invalid container entity ({0})", containerNetworkID.ToString());
                return true;
            }

            auto& container = registry->get<Components::Container>(containerEntity);
            containerPtr = &container;
        }

        containerPtr->AddToSlot(packet.slot, packet.guid);

        auto& item = registry->get<Components::Item>(itemEntity);

        Scripting::Zenith* zenith = Scripting::Util::Zenith::GetGlobal();
        zenith->CallEvent(MetaGen::Game::Lua::ContainerEvent::AddToSlot, MetaGen::Game::Lua::ContainerEventDataAddToSlot{ .containerIndex = packet.index + 1u, .slotIndex = packet.slot + 1u, .itemID = item.itemID, .count = item.count });

        return true;
    }
    bool HandleOnContainerRemoveFromSlot(Network::SocketID socketID, MetaGen::Shared::Packet::ServerContainerRemoveFromSlotPacket& packet)
    {
        if (packet.index >= 6)
        {
            NC_LOG_WARNING("Network : Received Container Remove From Slot for invalid container index ({0})", packet.index);
            return true;
        }

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& characterSingleton = registry->ctx().get<Singletons::CharacterSingleton>();
        auto& networkState = registry->ctx().get<Singletons::NetworkState>();

        Components::Container* containerPtr = nullptr;
        if (packet.index == 0)
        {
            if (!registry->valid(characterSingleton.baseContainerEntity))
            {
                NC_LOG_WARNING("Network : Received Container Remove From Slot for non existing base container");
                return true;
            }

            auto& baseContainer = registry->get<Components::Container>(characterSingleton.baseContainerEntity);
            containerPtr = &baseContainer;
        }
        else
        {
            if (!characterSingleton.containers[packet.index].IsValid())
            {
                NC_LOG_WARNING("Network : Received Container Remove From Slot for non existing container ({0})", packet.index);
                return true;
            }

            ObjectGUID containerNetworkID = characterSingleton.containers[packet.index];

            entt::entity containerEntity;
            if (!Util::Network::GetEntityIDFromObjectGUID(networkState, containerNetworkID, containerEntity))
            {
                NC_LOG_WARNING("Network : Received Container Remove From Slot for non existing container entity ({0})", containerNetworkID.ToString());
                return true;
            }

            if (!registry->valid(containerEntity))
            {
                NC_LOG_WARNING("Network : Received Container Remove From Slot for invalid container entity ({0})", containerNetworkID.ToString());
                return true;
            }

            auto& container = registry->get<Components::Container>(containerEntity);
            containerPtr = &container;
        }

        ObjectGUID itemNetworkID = containerPtr->GetItem(packet.slot);
        containerPtr->RemoveFromSlot(packet.slot);

        if (networkState.networkIDToEntity.contains(itemNetworkID))
        {
            entt::entity itemEntity = networkState.networkIDToEntity[itemNetworkID];
            registry->destroy(itemEntity);

            networkState.networkIDToEntity.erase(itemNetworkID);
        };

        Scripting::Zenith* zenith = Scripting::Util::Zenith::GetGlobal();
        zenith->CallEvent(MetaGen::Game::Lua::ContainerEvent::RemoveFromSlot, MetaGen::Game::Lua::ContainerEventDataRemoveFromSlot{ .containerIndex = packet.index + 1u, .slotIndex = packet.slot + 1u });

        return true;
    }
    bool HandleOnContainerSwapSlots(Network::SocketID socketID, MetaGen::Shared::Packet::SharedContainerSwapSlotsPacket& packet)
    {
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& characterSingleton = registry->ctx().get<Singletons::CharacterSingleton>();
        auto& networkState = registry->ctx().get<Singletons::NetworkState>();

        Components::Container* srcContainer = nullptr;
        Components::Container* dstContainer = nullptr;

        if (packet.srcContainer >= characterSingleton.containers.size() || packet.dstContainer >= characterSingleton.containers.size())
        {
            NC_LOG_WARNING("Network : Received Container Swap Slots with invalid container indices ({0}, {1})", packet.srcContainer, packet.dstContainer);
            return true;
        }

        if (packet.srcContainer == 0)
        {
            if (!registry->valid(characterSingleton.baseContainerEntity))
            {
                NC_LOG_WARNING("Network : Received Container Swap Slots for non existing base container");
                return true;
            }

            auto& baseContainer = registry->get<Components::Container>(characterSingleton.baseContainerEntity);
            srcContainer = &baseContainer;
        }
        else
        {
            if (!characterSingleton.containers[packet.srcContainer].IsValid())
            {
                NC_LOG_WARNING("Network : Received Container Swap Slots for non existing container ({0})", packet.srcContainer);
                return true;
            }

            ObjectGUID containerNetworkID = characterSingleton.containers[packet.srcContainer];

            entt::entity containerEntity;
            if (!Util::Network::GetEntityIDFromObjectGUID(networkState, containerNetworkID, containerEntity))
            {
                NC_LOG_WARNING("Network : Received Container Swap Slots for non existing container entity ({0})", containerNetworkID.ToString());
                return true;
            }

            if (!registry->valid(containerEntity))
            {
                NC_LOG_WARNING("Network : Received Container Swap Slots for invalid container entity ({0})", containerNetworkID.ToString());
                return true;
            }

            auto& container = registry->get<Components::Container>(containerEntity);
            srcContainer = &container;
        }

        if (packet.srcContainer == packet.dstContainer)
        {
            dstContainer = srcContainer;
        }
        else
        {
            if (packet.dstContainer == 0)
            {
                if (!registry->valid(characterSingleton.baseContainerEntity))
                {
                    NC_LOG_WARNING("Network : Received Container Swap Slots for non existing base container");
                    return true;
                }

                auto& baseContainer = registry->get<Components::Container>(characterSingleton.baseContainerEntity);
                dstContainer = &baseContainer;
            }
            else
            {
                if (!characterSingleton.containers[packet.dstContainer].IsValid())
                {
                    NC_LOG_WARNING("Network : Received Container Swap Slots for non existing container ({0})", packet.dstContainer);
                    return true;
                }

                ObjectGUID containerNetworkID = characterSingleton.containers[packet.dstContainer];

                entt::entity containerEntity;
                if (!Util::Network::GetEntityIDFromObjectGUID(networkState, containerNetworkID, containerEntity))
                {
                    NC_LOG_WARNING("Network : Received Container Swap Slots for non existing container entity ({0})", containerNetworkID.ToString());
                    return true;
                }

                if (!registry->valid(containerEntity))
                {
                    NC_LOG_WARNING("Network : Received Container Swap Slots for invalid container entity ({0})", containerNetworkID.ToString());
                    return true;
                }

                auto& container = registry->get<Components::Container>(containerEntity);
                dstContainer = &container;
            }
        }

        if (!srcContainer->SwapSlots(*dstContainer, packet.srcSlot, packet.dstSlot))
        {
            NC_LOG_WARNING("Network : Received Container Swap Slots with invalid slot indices ({0}, {1})", packet.srcSlot, packet.dstSlot);
            return true;
        }

        if (auto* unitEquipment = registry->try_get<Components::UnitEquipment>(characterSingleton.moverEntity))
        {
            if (packet.srcContainer == 0)
            {
                auto equippedSlot = static_cast<MetaGen::Shared::Unit::ItemEquipSlotEnum>(packet.srcSlot);
                if (equippedSlot >= MetaGen::Shared::Unit::ItemEquipSlotEnum::EquipmentStart && equippedSlot <= MetaGen::Shared::Unit::ItemEquipSlotEnum::EquipmentEnd)
                {
                    const ObjectGUID itemGUID = srcContainer->GetItem(packet.srcSlot);
                    bool hasItemInSlot = itemGUID.IsValid();
                    u32 itemID = 0;

                    if (hasItemInSlot)
                    {
                        entt::entity itemEntity;
                        if (Util::Network::GetEntityIDFromObjectGUID(networkState, itemGUID, itemEntity))
                        {
                            auto& item = registry->get<Components::Item>(itemEntity);
                            itemID = item.itemID;
                        }
                    }

                    unitEquipment->equipmentSlotToItemID[packet.srcSlot] = itemID;
                    unitEquipment->equipmentSlotToVisualItemID[packet.srcSlot] = itemID;
                    unitEquipment->dirtyItemIDSlots.insert(equippedSlot);
                    unitEquipment->dirtyVisualItemIDSlots.insert(equippedSlot);
                    registry->emplace_or_replace<Components::UnitEquipmentDirty>(characterSingleton.moverEntity);
                    registry->emplace_or_replace<Components::UnitVisualEquipmentDirty>(characterSingleton.moverEntity);
                }
            }

            if (packet.dstContainer == 0)
            {
                auto equippedSlot = static_cast<MetaGen::Shared::Unit::ItemEquipSlotEnum>(packet.dstSlot);
                if (equippedSlot >= MetaGen::Shared::Unit::ItemEquipSlotEnum::EquipmentStart && equippedSlot <= MetaGen::Shared::Unit::ItemEquipSlotEnum::EquipmentEnd)
                {
                    const ObjectGUID itemGUID = dstContainer->GetItem(packet.dstSlot);
                    bool hasItemInSlot = itemGUID.IsValid();
                    u32 itemID = 0;

                    if (hasItemInSlot)
                    {
                        entt::entity itemEntity;
                        if (Util::Network::GetEntityIDFromObjectGUID(networkState, itemGUID, itemEntity))
                        {
                            auto& item = registry->get<Components::Item>(itemEntity);
                            itemID = item.itemID;
                        }
                    }

                    unitEquipment->equipmentSlotToItemID[packet.dstSlot] = itemID;
                    unitEquipment->equipmentSlotToVisualItemID[packet.dstSlot] = itemID;
                    unitEquipment->dirtyItemIDSlots.insert(equippedSlot);
                    unitEquipment->dirtyVisualItemIDSlots.insert(equippedSlot);
                    registry->emplace_or_replace<Components::UnitEquipmentDirty>(characterSingleton.moverEntity);
                    registry->emplace_or_replace<Components::UnitVisualEquipmentDirty>(characterSingleton.moverEntity);
                }
            }
        }

        Scripting::Zenith* zenith = Scripting::Util::Zenith::GetGlobal();
        zenith->CallEvent(MetaGen::Game::Lua::ContainerEvent::SwapSlots, MetaGen::Game::Lua::ContainerEventDataSwapSlots{ .srcContainerIndex = packet.srcContainer + 1u, .destContainerIndex = packet.dstContainer + 1u, .srcSlotIndex = packet.srcSlot + 1u, .destSlotIndex = packet.dstSlot + 1u });

        return true;
    }

    bool HandleOnServerSpellCastResult(Network::SocketID socketID, MetaGen::Shared::Packet::ServerSpellCastResultPacket& packet)
    {
        if (packet.result == 0)
            return true;


        // TODO : Report spell cast failure to player
        return true;
    }

    bool HandleOnSendChatMessage(Network::SocketID socketID, MetaGen::Shared::Packet::ServerSendChatMessagePacket& packet)
    {
        std::string senderName = "";
        std::string channel = "System";

        if (packet.guid.IsValid())
        {
            entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
            auto& networkState = registry->ctx().get<Singletons::NetworkState>();

            entt::entity senderEntity;
            if (!Util::Network::GetEntityIDFromObjectGUID(networkState, packet.guid, senderEntity))
            {
                NC_LOG_WARNING("Network : Received Chat Message for non existing entity ({0})", packet.guid.ToString());
                return true;
            }

            if (!registry->valid(senderEntity))
            {
                NC_LOG_WARNING("Network : Received Chat Message for invalid entity ({0})", packet.guid.ToString());
                return true;
            }

            auto* senderUnit = registry->try_get<Components::Unit>(senderEntity);
            if (!senderUnit)
            {
                NC_LOG_WARNING("Network : Received Chat Message for entity without Unit Component ({0})", packet.guid.ToString());
                return true;
            }

            senderName = senderUnit->name;
            channel = "Say";
        }

        Scripting::Zenith* zenith = Scripting::Util::Zenith::GetGlobal();
        zenith->CallEvent(MetaGen::Game::Lua::GameEvent::ChatMessageReceived, MetaGen::Game::Lua::GameEventDataChatMessageReceived{ .sender = senderName, .channel = channel, .message = packet.message });

        return true;
    }

    bool HandleOnServerTriggerAdd(Network::SocketID socketID, MetaGen::Shared::Packet::ServerTriggerAddPacket& packet)
    {
        entt::registry& registry = *ServiceLocator::GetEnttRegistries()->gameRegistry;

        auto flags = static_cast<MetaGen::Shared::ProximityTrigger::ProximityTriggerFlagEnum>(packet.flags);
        ECS::Util::ProximityTriggerUtil::CreateTrigger(registry, packet.triggerID, packet.name, flags, packet.mapID, packet.position, packet.extents);
        return true;
    }

    bool HandleOnServerTriggerRemove(Network::SocketID socketID, MetaGen::Shared::Packet::ServerTriggerRemovePacket& packet)
    {
        entt::registry& registry = *ServiceLocator::GetEnttRegistries()->gameRegistry;
        ECS::Util::ProximityTriggerUtil::DestroyTrigger(registry, packet.triggerID);
        return true;
    }

    bool HandleOnServerUnitAddAura(Network::SocketID socketID, MetaGen::Shared::Packet::ServerUnitAddAuraPacket& packet)
    {
        entt::registry& registry = *ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& networkState = registry.ctx().get<Singletons::NetworkState>();

        entt::entity unitID;
        if (!Util::Network::GetEntityIDFromObjectGUID(networkState, packet.guid, unitID))
        {
            NC_LOG_WARNING("Network : Received UnitAddAura for non-existent entity ({0})", packet.guid.ToString());
            return true;
        }

        auto& unitAuraInfo = registry.get<Components::UnitAuraInfo>(unitID);

        u8 disposition = static_cast<u8>(MetaGen::Shared::Spell::AuraDispositionEnum::None);
        u8 dispelType = static_cast<u8>(MetaGen::Shared::Spell::AuraDispelTypeEnum::None);
        entt::registry* dbRegistry = ServiceLocator::GetEnttRegistries()->dbRegistry;
        auto& clientDBSingleton = dbRegistry->ctx().get<Singletons::ClientDBSingleton>();
        if (clientDBSingleton.Has(ClientDBHash::SpellAura))
        {
            auto* spellAuraStorage = clientDBSingleton.Get(ClientDBHash::SpellAura);
            if (spellAuraStorage->Has(packet.spellID))
            {
                const auto& spellAura = spellAuraStorage->Get<MetaGen::Shared::ClientDB::SpellAuraRecord>(packet.spellID);
                disposition = spellAura.disposition;
                dispelType = spellAura.dispelType;
            }
        }

        u32 auraIndex = static_cast<u32>(unitAuraInfo.auras.size());
        auto& auraInfo = unitAuraInfo.auras.emplace_back();
        auraInfo.unitID = entt::to_integral(unitID);
        auraInfo.auraID = packet.auraInstanceID;
        auraInfo.spellID = packet.spellID;
        auraInfo.expireTimestamp = CalculateAuraExpirationTimestamp(packet.duration);
        auraInfo.stacks = packet.stacks;
        auraInfo.disposition = disposition;
        auraInfo.dispelType = dispelType;

        unitAuraInfo.auraIDToAuraIndex[packet.auraInstanceID] = auraIndex;

        Scripting::Zenith* zenith = Scripting::Util::Zenith::GetGlobal();
        zenith->CallEvent(MetaGen::Game::Lua::UnitEvent::AuraAdd, MetaGen::Game::Lua::UnitEventDataAuraAdd{ .unitID = entt::to_integral(unitID), .auraID = packet.auraInstanceID, .spellID = packet.spellID, .duration = packet.duration, .stacks = packet.stacks });

        return true;
    }

    bool HandleOnServerUnitUpdateAura(Network::SocketID socketID, MetaGen::Shared::Packet::ServerUnitUpdateAuraPacket& packet)
    {
        entt::registry& registry = *ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& networkState = registry.ctx().get<Singletons::NetworkState>();

        entt::entity unitID;
        if (!Util::Network::GetEntityIDFromObjectGUID(networkState, packet.guid, unitID))
        {
            NC_LOG_WARNING("Network : Received UnitUpdateAura for non-existent entity ({0})", packet.guid.ToString());
            return true;
        }

        auto& unitAuraInfo = registry.get<Components::UnitAuraInfo>(unitID);
        if (!unitAuraInfo.auraIDToAuraIndex.contains(packet.auraInstanceID))
        {
            NC_LOG_WARNING("Network : Received UnitUpdateAura for non-existent aura ({0}) on entity ({1})", packet.auraInstanceID, packet.guid.ToString());
            return true;
        }

        u32 auraIndex = unitAuraInfo.auraIDToAuraIndex[packet.auraInstanceID];
        auto& auraInfo = unitAuraInfo.auras[auraIndex];
        auraInfo.expireTimestamp = CalculateAuraExpirationTimestamp(packet.duration);
        auraInfo.stacks = packet.stacks;

        Scripting::Zenith* zenith = Scripting::Util::Zenith::GetGlobal();
        zenith->CallEvent(MetaGen::Game::Lua::UnitEvent::AuraUpdate, MetaGen::Game::Lua::UnitEventDataAuraUpdate{ .unitID = entt::to_integral(unitID), .auraID = packet.auraInstanceID, .duration = packet.duration, .stacks = packet.stacks });

        return true;
    }

    bool HandleOnServerUnitRemoveAura(Network::SocketID socketID, MetaGen::Shared::Packet::ServerUnitRemoveAuraPacket& packet)
    {
        entt::registry& registry = *ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& networkState = registry.ctx().get<Singletons::NetworkState>();

        entt::entity unitID;
        if (!Util::Network::GetEntityIDFromObjectGUID(networkState, packet.guid, unitID))
        {
            NC_LOG_WARNING("Network : Received UnitRemoveAura for non-existent entity ({0})", packet.guid.ToString());
            return true;
        }

        auto& unitAuraInfo = registry.get<Components::UnitAuraInfo>(unitID);
        if (!unitAuraInfo.auraIDToAuraIndex.contains(packet.auraInstanceID))
        {
            NC_LOG_WARNING("Network : Received UnitRemoveAura for non-existent aura ({0}) on entity ({1})", packet.auraInstanceID, packet.guid.ToString());
            return true;
        }

        Scripting::Zenith* zenith = Scripting::Util::Zenith::GetGlobal();
        zenith->CallEvent(MetaGen::Game::Lua::UnitEvent::AuraRemove, MetaGen::Game::Lua::UnitEventDataAuraRemove{ .unitID = entt::to_integral(unitID), .auraID = packet.auraInstanceID });

        u32 auraIndex = unitAuraInfo.auraIDToAuraIndex[packet.auraInstanceID];
        unitAuraInfo.auras.erase(unitAuraInfo.auras.begin() + auraIndex);

        // Vector erasure shifts all following indices, so rebuild the instance lookup.
        unitAuraInfo.auraIDToAuraIndex.clear();

        for (u32 i = 0; i < unitAuraInfo.auras.size(); ++i)
        {
            unitAuraInfo.auraIDToAuraIndex[unitAuraInfo.auras[i].auraID] = i;
        }

        return true;
    }

    void NetworkConnection::Init(entt::registry& registry)
    {
        entt::registry::context& ctx = registry.ctx();

        auto& networkState = ctx.emplace<Singletons::NetworkState>();
        Util::Faction::SetEventCallbacks(registry, EmitUnitReactionChanged, EmitReputationChanged);

        // Setup NetworkState
        {
            networkState.resolver = std::make_shared<asio::ip::tcp::resolver>(networkState.asioContext);
            networkState.client = std::make_unique<Network::Client>(networkState.asioContext, networkState.resolver);
            networkState.networkIDToEntity.reserve(1024);
            networkState.entityToNetworkID.reserve(1024);
            networkState.networkVisTree = std::make_unique<RTree<ObjectGUID, f32, 3>>();
            networkState.gameMessageRouter = std::make_unique<Network::GameMessageRouter>();

            networkState.gameMessageRouter->RegisterPacketHandler(Network::ConnectionStatus::Connected, HandleOnAuthChallenge);
            networkState.gameMessageRouter->RegisterPacketHandler(Network::ConnectionStatus::Connected, HandleOnAuthProof);
            networkState.gameMessageRouter->RegisterPacketHandler(Network::ConnectionStatus::Connected, HandleOnConnectResult);
            networkState.gameMessageRouter->RegisterPacketHandler(Network::ConnectionStatus::Connected, HandleOnWorldTransfer);
            networkState.gameMessageRouter->RegisterPacketHandler(Network::ConnectionStatus::Connected, HandleOnLoadMap);
            networkState.gameMessageRouter->RegisterPacketHandler(Network::ConnectionStatus::Connected, HandleOnCharacterLogout);
            networkState.gameMessageRouter->RegisterPacketHandler(Network::ConnectionStatus::Connected, HandleOnPong);
            networkState.gameMessageRouter->RegisterPacketHandler(Network::ConnectionStatus::Connected, HandleOnServerUpdateStats);

            networkState.gameMessageRouter->RegisterPacketHandler(Network::ConnectionStatus::Connected, HandleOnCheatCommandResult);

            networkState.gameMessageRouter->SetMessageHandler(MetaGen::Shared::Packet::ServerInteractionSnapshotPacket::PACKET_ID, Network::GameMessageHandler(Network::ConnectionStatus::Connected, 0u, -1, &HandleOnInteractionSnapshot));
            networkState.gameMessageRouter->RegisterPacketHandler(Network::ConnectionStatus::Connected, HandleOnInteractionClose);
            networkState.gameMessageRouter->RegisterPacketHandler(Network::ConnectionStatus::Connected, HandleOnInteractionResult);
            networkState.gameMessageRouter->RegisterPacketHandler(Network::ConnectionStatus::Connected, HandleOnUnitInteractionUpdate);

            networkState.gameMessageRouter->RegisterPacketHandler(Network::ConnectionStatus::Connected, HandleOnUnitFactionUpdate);
            networkState.gameMessageRouter->RegisterPacketHandler(Network::ConnectionStatus::Connected, HandleOnReputationUpdate);
            networkState.gameMessageRouter->RegisterPacketHandler(Network::ConnectionStatus::Connected, HandleOnFactionPerceptionOverrideUpdate);

            networkState.gameMessageRouter->RegisterPacketHandler(Network::ConnectionStatus::Connected, HandleOnUnitAdd);
            networkState.gameMessageRouter->RegisterPacketHandler(Network::ConnectionStatus::Connected, HandleOnUnitRemove);
            networkState.gameMessageRouter->RegisterPacketHandler(Network::ConnectionStatus::Connected, HandleOnUnitEquippedItemUpdate);
            networkState.gameMessageRouter->RegisterPacketHandler(Network::ConnectionStatus::Connected, HandleOnUnitVisualItemUpdate);
            networkState.gameMessageRouter->RegisterPacketHandler(Network::ConnectionStatus::Connected, HandleOnUnitPowerUpdate);
            networkState.gameMessageRouter->RegisterPacketHandler(Network::ConnectionStatus::Connected, HandleOnUnitResistanceUpdate);
            networkState.gameMessageRouter->RegisterPacketHandler(Network::ConnectionStatus::Connected, HandleOnUnitStatUpdate);
            networkState.gameMessageRouter->RegisterPacketHandler(Network::ConnectionStatus::Connected, HandleOnUnitTargetUpdate);
            networkState.gameMessageRouter->RegisterPacketHandler(Network::ConnectionStatus::Connected, HandleOnUnitCastSpell);
            networkState.gameMessageRouter->RegisterPacketHandler(Network::ConnectionStatus::Connected, HandleOnUnitAttack);
            networkState.gameMessageRouter->RegisterPacketHandler(Network::ConnectionStatus::Connected, HandleOnUnitAutoAttackState);
            networkState.gameMessageRouter->RegisterPacketHandler(Network::ConnectionStatus::Connected, HandleOnUnitSetMover);
            networkState.gameMessageRouter->RegisterPacketHandler(Network::ConnectionStatus::Connected, HandleOnUnitMove);
            networkState.gameMessageRouter->RegisterPacketHandler(Network::ConnectionStatus::Connected, HandleOnUnitMoveStop);
            networkState.gameMessageRouter->RegisterPacketHandler(Network::ConnectionStatus::Connected, HandleOnUnitTeleport);

            networkState.gameMessageRouter->RegisterPacketHandler(Network::ConnectionStatus::Connected, HandleOnItemAdd);

            networkState.gameMessageRouter->RegisterPacketHandler(Network::ConnectionStatus::Connected, HandleOnContainerAdd);
            networkState.gameMessageRouter->RegisterPacketHandler(Network::ConnectionStatus::Connected, HandleOnContainerAddToSlot);
            networkState.gameMessageRouter->RegisterPacketHandler(Network::ConnectionStatus::Connected, HandleOnContainerRemoveFromSlot);
            networkState.gameMessageRouter->RegisterPacketHandler(Network::ConnectionStatus::Connected, HandleOnContainerSwapSlots);

            networkState.gameMessageRouter->RegisterPacketHandler(Network::ConnectionStatus::Connected, HandleOnServerSpellCastResult);

            networkState.gameMessageRouter->RegisterPacketHandler(Network::ConnectionStatus::Connected, HandleOnSendChatMessage);
            networkState.gameMessageRouter->RegisterPacketHandler(Network::ConnectionStatus::Connected, HandleOnServerTriggerAdd);
            networkState.gameMessageRouter->RegisterPacketHandler(Network::ConnectionStatus::Connected, HandleOnServerTriggerRemove);

            networkState.gameMessageRouter->RegisterPacketHandler(Network::ConnectionStatus::Connected, HandleOnServerUnitAddAura);
            networkState.gameMessageRouter->RegisterPacketHandler(Network::ConnectionStatus::Connected, HandleOnServerUnitUpdateAura);
            networkState.gameMessageRouter->RegisterPacketHandler(Network::ConnectionStatus::Connected, HandleOnServerUnitRemoveAura);

            networkState.gameMessageRouter->SetMessageHandler(MetaGen::Shared::Packet::ServerCharacterListPacket::PACKET_ID, Network::GameMessageHandler(Network::ConnectionStatus::Connected, 0u, -1, &HandleOnCharacterList));
            networkState.gameMessageRouter->SetMessageHandler(MetaGen::Shared::Packet::ServerObjectNetFieldUpdatePacket::PACKET_ID, Network::GameMessageHandler(Network::ConnectionStatus::Connected, 0u, -1, &HandleOnObjectNetFieldUpdate));
            networkState.gameMessageRouter->SetMessageHandler(MetaGen::Shared::Packet::ServerUnitNetFieldUpdatePacket::PACKET_ID, Network::GameMessageHandler(Network::ConnectionStatus::Connected, 0u, -1, &HandleOnUnitNetFieldUpdate));
            networkState.gameMessageRouter->SetMessageHandler(MetaGen::Shared::Packet::ServerSendCombatEventPacket::PACKET_ID, Network::GameMessageHandler(Network::ConnectionStatus::Connected, 0u, -1, &HandleOnCombatEvent));
            networkState.gameMessageRouter->SetMessageHandler(MetaGen::Shared::Packet::ServerPathVisualizationPacket::PACKET_ID, Network::GameMessageHandler(Network::ConnectionStatus::Connected, 0u, -1, &HandleOnVisualizePath));
            networkState.gameMessageRouter->SetMessageHandler(MetaGen::Shared::Packet::ServerDatabaseEditorSnapshotBeginPacket::PACKET_ID, Network::GameMessageHandler(Network::ConnectionStatus::Connected, 0u, -1, &HandleOnDatabaseEditorSnapshotBegin));
            networkState.gameMessageRouter->SetMessageHandler(MetaGen::Shared::Packet::ServerDatabaseEditorSnapshotChunkPacket::PACKET_ID, Network::GameMessageHandler(Network::ConnectionStatus::Connected, 0u, -1, &HandleOnDatabaseEditorSnapshotChunk));
            networkState.gameMessageRouter->SetMessageHandler(MetaGen::Shared::Packet::ServerDatabaseEditorSnapshotEndPacket::PACKET_ID, Network::GameMessageHandler(Network::ConnectionStatus::Connected, 0u, -1, &HandleOnDatabaseEditorSnapshotEnd));
            networkState.gameMessageRouter->SetMessageHandler(MetaGen::Shared::Packet::ServerDatabaseEditorChangeSetPacket::PACKET_ID, Network::GameMessageHandler(Network::ConnectionStatus::Connected, 0u, -1, &HandleOnDatabaseEditorChangeSet));
            networkState.gameMessageRouter->RegisterPacketHandler(Network::ConnectionStatus::Connected, HandleOnDatabaseEditorMutationResult);
            networkState.gameMessageRouter->RegisterPacketHandler(Network::ConnectionStatus::Connected, HandleOnDevelopmentActionResult);
            networkState.gameMessageRouter->SetMessageHandler(MetaGen::Shared::Packet::ServerDevelopmentTransferBeginPacket::PACKET_ID, Network::GameMessageHandler(Network::ConnectionStatus::Connected, 0u, -1, &HandleOnDevelopmentTransferBegin));
            networkState.gameMessageRouter->SetMessageHandler(MetaGen::Shared::Packet::ServerDevelopmentTransferChunkPacket::PACKET_ID, Network::GameMessageHandler(Network::ConnectionStatus::Connected, 0u, -1, &HandleOnDevelopmentTransferChunk));
            networkState.gameMessageRouter->SetMessageHandler(MetaGen::Shared::Packet::ServerDevelopmentTransferEndPacket::PACKET_ID, Network::GameMessageHandler(Network::ConnectionStatus::Connected, 0u, -1, &HandleOnDevelopmentTransferEnd));
            networkState.gameMessageRouter->RegisterPacketHandler(Network::ConnectionStatus::Connected, HandleOnCreatureAIDevelopmentInfo);

            networkState.unitNetFieldListener.RegisterFieldListener(MetaGen::Shared::NetField::UnitNetFieldEnum::DisplayID, [](entt::entity entity, ObjectGUID guid, MetaGen::Shared::NetField::UnitNetFieldEnum field)
            {
                auto* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
                auto& networkState = registry->ctx().get<Singletons::NetworkState>();
                ModelLoader* modelLoader = ServiceLocator::GetGameRenderer()->GetModelLoader();

                auto& unitFields = registry->get<Components::UnitFields>(entity);
                u32 levelRaceGenderClassPacked = unitFields.fields.GetField<u32>(MetaGen::Shared::NetField::UnitNetFieldEnum::LevelRaceGenderClassPacked);

                u32 displayID = unitFields.fields.GetField<u32>(field);
                GameDefine::UnitRace race = static_cast<GameDefine::UnitRace>((levelRaceGenderClassPacked >> 16) & 0x7F);
                GameDefine::UnitGender gender = static_cast<GameDefine::UnitGender>((levelRaceGenderClassPacked >> 23) & 0x3);

                auto& model = registry->get<ECS::Components::Model>(entity);
                auto& displayInfo = registry->get<Components::DisplayInfo>(entity);

                displayInfo.displayID = displayID;
                displayInfo.race = race;
                displayInfo.gender = gender;

                if (!modelLoader->LoadDisplayIDForEntity(entity, model, Database::Unit::DisplayInfoType::Creature, displayID))
                {
                    NC_LOG_WARNING("Network : Failed to load DisplayID({1}) for entity ({0})", guid.ToString(), displayID);

                    modelLoader->LoadDisplayIDForEntity(entity, model, Database::Unit::DisplayInfoType::Creature, 10045);
                    return true;
                }

                return true;
            });
        }
    }

    void NetworkConnection::Update(entt::registry& registry, f32 deltaTime)
    {
        ZoneScopedN("ECS::NetworkConnection");

        entt::registry::context& ctx = registry.ctx();
        auto& networkState = ctx.get<Singletons::NetworkState>();

        // Restart AsioThread If Needed
        {
            if (Util::Network::IsConnected(networkState))
            {
                if (!networkState.asioThread.joinable())
                {
                    networkState.asioThread = std::thread([&]
                    {
                        if (networkState.asioContext.stopped())
                            networkState.asioContext.restart();

                        networkState.asioContext.run();
                    });
                }
            }
        }

        static bool wasConnected = false;
        if (Util::Network::IsConnected(networkState))
        {
            auto currentTime = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

            if (!wasConnected)
            {
                // Just connected
                wasConnected = true;
                networkState.pingInfo.lastPingTime = currentTime;
                DeleteActiveCharacterController(registry, true);
            }
            else
            {
                u64 timeDiff = currentTime - networkState.pingInfo.lastPingTime;
                if (timeDiff >= Singletons::NetworkState::PING_INTERVAL)
                {
                    std::shared_ptr<Bytebuffer> buffer = Bytebuffer::Borrow<16>();
                    if (Util::Network::SendPacket(networkState, MetaGen::Shared::Packet::ClientPingPacket{ .ping = networkState.pingInfo.ping }))
                    {
                        networkState.pingInfo.lastPingTime = currentTime;
                    }
                }

                if (networkState.pingInfo.lastPongTime != 0u)
                {
                    u64 timeDiff = currentTime - networkState.pingInfo.lastPongTime;
                    if (currentTime - networkState.pingInfo.lastPongTime > Singletons::NetworkState::PING_INTERVAL)
                    {
                        networkState.pingInfo.ping = static_cast<u16>(timeDiff);
                    }
                }

                // Visualize Path
                {
                    DebugRenderer* debugRenderer = ServiceLocator::GetGameRenderer()->GetDebugRenderer();

                    u32 numPointsToVisualize = static_cast<u32>(networkState.pathToVisualize.size());
                    for (u32 i = 0; i < numPointsToVisualize; i++)
                    {
                        const vec3& point = networkState.pathToVisualize[i];
                        debugRenderer->DrawSphere3D(point, 0.5f, 8, Color::Red);

                        if (i > 0)
                        {
                            const vec3& lastPoint = networkState.pathToVisualize[i - 1];
                            debugRenderer->DrawLine3D(lastPoint, point, Color::Blue);
                        }
                    }
                }
            }

            // Check Map Loaded Event
            {
                Util::EventUtil::OnEvent<Components::MapLoadedEvent>([&](const Components::MapLoadedEvent& event)
                {
                    networkState.isLoadingMap = false;
                });
            }
        }
        else
        {
            if (wasConnected)
            {
                // Just Disconnected
                wasConnected = false;

                NC_LOG_WARNING("Network : Disconnected");

                CleanupNetworkWorldEntities(registry);
                Util::Faction::ResetOwnerState(registry);

                entt::registry::context& dbContext = ServiceLocator::GetEnttRegistries()->dbRegistry->ctx();
                if (dbContext.contains<Editor::SpellEditorBackend>())
                    dbContext.erase<Editor::SpellEditorBackend>();
                if (dbContext.contains<Editor::SpellEditorData>())
                    dbContext.erase<Editor::SpellEditorData>();
                if (dbContext.contains<Editor::MapEditorBackend>())
                    dbContext.erase<Editor::MapEditorBackend>();
                if (dbContext.contains<Editor::MapEditorData>())
                    dbContext.erase<Editor::MapEditorData>();
                if (dbContext.contains<Editor::InteractionEditorBackend>())
                    dbContext.erase<Editor::InteractionEditorBackend>();
                if (dbContext.contains<Editor::InteractionEditorData>())
                    dbContext.erase<Editor::InteractionEditorData>();
                if (dbContext.contains<Editor::CreatureAIEditorBackend>())
                    dbContext.erase<Editor::CreatureAIEditorBackend>();

                networkState.authInfo.Reset();
                networkState.characterListInfo.Reset();
                networkState.pingInfo.Reset();

                networkState.asioContext.stop();

                if (networkState.asioThread.joinable())
                    networkState.asioThread.join();

                ServiceLocator::GetLuaManager()->SetDirty();

                MapLoader* mapLoader = ServiceLocator::GetGameRenderer()->GetMapLoader();
                mapLoader->UnloadMap();
            }

            return;
        }

        // Handle 'SocketMessageEvent'
        {
            moodycamel::ConcurrentQueue<Network::SocketMessageEvent>& messageEvents = networkState.client->GetMessageEvents();

            Network::SocketMessageEvent messageEvent;
            while (!networkState.isLoadingMap && messageEvents.try_dequeue(messageEvent))
            {
                Network::MessageHeader messageHeader;
                if (networkState.gameMessageRouter->GetMessageHeader(messageEvent.message, messageHeader))
                {
                    if (networkState.gameMessageRouter->HasValidHandlerForHeader(messageHeader))
                    {
                        if (networkState.gameMessageRouter->CallHandler(messageEvent.socketID, messageHeader, messageEvent.message))
                            continue;
                    }
                }

                // Failed to Call Handler, Close Socket
                {
                    networkState.client->Stop();
                    break;
                }
            }
        }

    }
}
