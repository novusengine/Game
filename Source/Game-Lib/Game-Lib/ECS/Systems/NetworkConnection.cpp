#include "NetworkConnection.h"
#include "CharacterController.h"

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
#include "Game-Lib/ECS/Components/ProximityTrigger.h"
#include "Game-Lib/ECS/Components/Tags.h"
#include "Game-Lib/ECS/Components/Unit.h"
#include "Game-Lib/ECS/Components/UnitAuraInfo.h"
#include "Game-Lib/ECS/Components/UnitCustomization.h"
#include "Game-Lib/ECS/Components/UnitEquipment.h"
#include "Game-Lib/ECS/Components/UnitMovementOverTime.h"
#include "Game-Lib/ECS/Components/UnitPowersComponent.h"
#include "Game-Lib/ECS/Components/UnitResistancesComponent.h"
#include "Game-Lib/ECS/Components/UnitStatsComponent.h"
#include "Game-Lib/ECS/Singletons/CharacterSingleton.h"
#include "Game-Lib/ECS/Singletons/JoltState.h"
#include "Game-Lib/ECS/Singletons/NetworkState.h"
#include "Game-Lib/ECS/Singletons/OrbitalCameraSettings.h"
#include "Game-Lib/ECS/Singletons/ProximityTriggerSingleton.h"
#include "Game-Lib/ECS/Singletons/Database/ClientDBSingleton.h"
#include "Game-Lib/ECS/Singletons/Database/ItemSingleton.h"
#include "Game-Lib/ECS/Util/EventUtil.h"
#include "Game-Lib/ECS/Util/MessageBuilderUtil.h"
#include "Game-Lib/ECS/Util/ProximityTriggerUtil.h"
#include "Game-Lib/ECS/Util/Transforms.h"
#include "Game-Lib/ECS/Util/Database/ItemUtil.h"
#include "Game-Lib/ECS/Util/Network/NetworkUtil.h"
#include "Game-Lib/Gameplay/MapLoader.h"
#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Rendering/Model/ModelLoader.h"
#include "Game-Lib/Scripting/Util/ZenithUtil.h"
#include "Game-Lib/Util/ServiceLocator.h"
#include "Game-Lib/Util/UnitUtil.h"

#include <Base/CVarSystem/CVarSystem.h>
#include <Base/Util/DebugHandler.h>

#include <Gameplay/Network/GameMessageRouter.h>

#include <Meta/Generated/Shared/ProximityTriggerEnum.h>

#include <Network/Client.h>
#include <Network/Define.h>

#include <Meta/Generated/Game/LuaEnum.h>
#include <Meta/Generated/Game/LuaEvent.h>
#include <Meta/Generated/Shared/CombatLogEnum.h>
#include <Meta/Generated/Shared/NetworkPacket.h>
#include <Meta/Generated/Shared/UnitEnum.h>

#include <Scripting/LuaManager.h>
#include <Scripting/Zenith.h>

#include <entt/entt.hpp>
#include <imgui/ImGuiNotify.hpp>
#include <libsodium/sodium.h>

#include <chrono>
#include <numeric>

namespace ECS::Systems
{
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
            failed |= !message.buffer->GetU16(entry.mapID);
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
            zenith->CallEvent(Generated::LuaGameEventEnum::CharacterListChanged, Generated::LuaGameEventDataCharacterListChanged{});
        }

        return true;
    }
    bool HandleOnCombatEvent(Network::SocketID socketID, Network::Message& message)
    {
        Generated::CombatLogEventsEnum eventID;
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
            case Generated::CombatLogEventsEnum::DamageDealt:
            case Generated::CombatLogEventsEnum::HealingDone:
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

                std::string result = "";

                if (eventID == Generated::CombatLogEventsEnum::DamageDealt)
                {
                    // Damage Dealt
                    if (overValue)
                    {
                        result = std::format("{} dealt {:.2f} damage to {} (Overkill: {:.2f})", sourceUnit->name, value, targetUnit->name, overValue);
                    }
                    else
                    {
                        result = std::format("{} dealt {:.2f} damage to {}", sourceUnit->name, value, targetUnit->name);
                    }
                }
                else
                {
                    // Healing Taken

                    if (overValue)
                    {
                        result = std::format("{} healed {} for {:.2f} (Overheal: {:.2f})", sourceUnit->name, targetUnit->name, value, overValue);
                    }
                    else
                    {
                        result = std::format("{} healed {} for {:.2f}", sourceUnit->name, targetUnit->name, value);
                    }
                }

                ImGui::InsertNotification({ ImGuiToastType::Success, 3000, "%s", result.c_str() });
                break;
            }

            case Generated::CombatLogEventsEnum::Resurrected:
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

                std::string result = std::format("{} resurrected {} (Restored Health: {:.2f})", sourceUnit->name, targetUnit->name, restoredHealth);
                ImGui::InsertNotification({ ImGuiToastType::Success, 3000, "%s", result.c_str() });
                break;
            }

            default:
            {
                break;
            }
        }
        return true;
    }

    bool HandleOnAuthChallenge(Network::SocketID socketID, Generated::ServerAuthChallengePacket& packet)
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

        Generated::ClientAuthChallengePacket responsePacket;
        std::memcpy(responsePacket.challenge.data(), response1, crypto_spake_RESPONSE1BYTES);
        Util::Network::SendPacket(networkState, responsePacket);

        return true;
    }
    bool HandleOnAuthProof(Network::SocketID socketID, Generated::ServerAuthProofPacket& packet)
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

        Generated::ClientAuthProofPacket authProofPacket;
        std::memcpy(authProofPacket.proof.data(), response3, crypto_spake_RESPONSE3BYTES);
        Util::Network::SendPacket(networkState, authProofPacket);

        return true;
    }
    bool HandleOnConnectResult(Network::SocketID socketID, Generated::ConnectResultPacket& packet)
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
    bool HandleOnWorldTransfer(Network::SocketID socketID, Generated::ServerWorldTransferPacket& packet)
    {
        entt::registry* gameRegistry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& networkState = gameRegistry->ctx().get<Singletons::NetworkState>();

        CharacterController::DeleteCharacterController(*gameRegistry, false);

        for (auto& [entity, networkID] : networkState.entityToNetworkID)
        {
            if (!gameRegistry->valid(entity))
                continue;

            if (auto* attachmentData = gameRegistry->try_get<Components::AttachmentData>(entity))
            {
                for (auto& pair : attachmentData->attachmentToInstance)
                {
                    ::Util::Unit::RemoveItemFromAttachment(*gameRegistry, entity, pair.first);
                }
            }

            if (auto* model = gameRegistry->try_get<Components::Model>(entity))
            {
                if (model->instanceID != std::numeric_limits<u32>().max())
                {
                    ModelLoader* modelLoader = ServiceLocator::GetGameRenderer()->GetModelLoader();
                    modelLoader->UnloadModelForEntity(entity, *model);
                }
            }

            gameRegistry->destroy(entity);
        }

        networkState.isLoadingMap = false;
        networkState.isInWorld = true;
        networkState.entityToNetworkID.clear();
        networkState.networkIDToEntity.clear();
        networkState.networkVisTree->RemoveAll();

        ServiceLocator::GetLuaManager()->SetDirty();

        MapLoader* mapLoader = ServiceLocator::GetGameRenderer()->GetMapLoader();
        mapLoader->UnloadMapImmediately();

        return true;
    }
    bool HandleOnLoadMap(Network::SocketID socketID, Generated::ServerLoadMapPacket& packet)
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

        const auto& map = mapStorage->Get<Generated::MapRecord>(packet.mapID);
        const std::string& mapInternalName = mapStorage->GetString(map.nameInternal);

        u32 internalMapNameHash = StringUtils::fnv1a_32(mapInternalName.c_str(), mapInternalName.length());
        mapLoader->LoadMap(internalMapNameHash);
        networkState.isLoadingMap = true;
        return true;
    }
    bool HandleOnCharacterLogout(Network::SocketID socketID, Generated::ServerCharacterLogoutPacket& packet)
    {
        entt::registry* gameRegistry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& networkState = gameRegistry->ctx().get<Singletons::NetworkState>();

        CharacterController::DeleteCharacterController(*gameRegistry, false);

        for (auto& [entity, networkID] : networkState.entityToNetworkID)
        {
            if (!gameRegistry->valid(entity))
                continue;

            if (auto* attachmentData = gameRegistry->try_get<Components::AttachmentData>(entity))
            {
                for (auto& pair : attachmentData->attachmentToInstance)
                {
                    ::Util::Unit::RemoveItemFromAttachment(*gameRegistry, entity, pair.first);
                }
            }

            if (auto* model = gameRegistry->try_get<Components::Model>(entity))
            {
                if (model->instanceID != std::numeric_limits<u32>().max())
                {
                    ModelLoader* modelLoader = ServiceLocator::GetGameRenderer()->GetModelLoader();
                    modelLoader->UnloadModelForEntity(entity, *model);
                }
            }

            gameRegistry->destroy(entity);
        }

        networkState.isLoadingMap = false;
        networkState.isInWorld = false;
        networkState.characterListInfo.characterSelected = false;
        networkState.entityToNetworkID.clear();
        networkState.networkIDToEntity.clear();
        networkState.networkVisTree->RemoveAll();

        ServiceLocator::GetLuaManager()->SetDirty();

        MapLoader* mapLoader = ServiceLocator::GetGameRenderer()->GetMapLoader();
        mapLoader->UnloadMapImmediately();

        return true;
    }
    bool HandleOnPong(Network::SocketID socketID, Generated::PongPacket& packet)
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
    bool HandleOnServerUpdateStats(Network::SocketID socketID, Generated::ServerUpdateStatsPacket& packet)
    {
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& networkState = registry->ctx().get<Singletons::NetworkState>();

        networkState.pingInfo.serverUpdateDiff = packet.serverTickTime;

        return true;
    }

    bool HandleOnCheatCommandResult(Network::SocketID socketID, Generated::CheatCommandResultPacket& packet)
    {
        return true;
    }

    bool HandleOnUnitAdd(Network::SocketID socketID, Generated::UnitAddPacket& packet)
    {
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& networkState = registry->ctx().get<Singletons::NetworkState>();

        if (Util::Network::IsObjectGUIDKnown(networkState, packet.guid))
        {
            NC_LOG_WARNING("Network : Received UnitAdd for already existing entity ({0})", packet.guid.ToString());
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

        TransformSystem& transformSystem = TransformSystem::Get(*registry);

        quat rotation = quat(glm::vec3(packet.pitchYaw.x, packet.pitchYaw.y, 0.0f));
        transformSystem.SetWorldPosition(newEntity, packet.position);
        transformSystem.SetWorldRotation(newEntity, rotation);
        transformSystem.SetLocalScale(newEntity, packet.scale);

        networkState.networkIDToEntity[packet.guid] = newEntity;
        networkState.entityToNetworkID[newEntity] = packet.guid;

        vec3 min = packet.position - (packet.scale * 0.5f);
        vec3 max = packet.position + (packet.scale * 0.5f);
        if (auto* aabb = registry->try_get<Components::AABB>(newEntity))
        {
            min = packet.position + (aabb->centerPos - aabb->extents);
            max = packet.position + (aabb->centerPos + aabb->extents);
        }
        networkState.networkVisTree->Insert(&min.x, &max.x, packet.guid);

        Scripting::Zenith* zenith = Scripting::Util::Zenith::GetGlobal();
        zenith->CallEvent(Generated::LuaUnitEventEnum::Add, Generated::LuaUnitEventDataAdd{
            .unitID = entt::to_integral(newEntity)
        });

        return true;
    }
    bool HandleOnUnitRemove(Network::SocketID socketID, Generated::UnitRemovePacket& packet)
    {
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& networkState = registry->ctx().get<Singletons::NetworkState>();

        entt::entity entity;
        if (!Util::Network::GetEntityIDFromObjectGUID(networkState, packet.guid, entity))
        {
            NC_LOG_WARNING("Network : Received UnitRemove for unknown entity ({0})", packet.guid.ToString());
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

        registry->destroy(entity);

        Scripting::Zenith* zenith = Scripting::Util::Zenith::GetGlobal();
        zenith->CallEvent(Generated::LuaUnitEventEnum::Remove, Generated::LuaUnitEventDataRemove{
            .unitID = entt::to_integral(entity)
        });

        return true;
    }
//    bool HandleOnUnitDisplayInfoUpdate(Network::SocketID socketID, Generated::UnitDisplayInfoUpdatePacket& packet)
//    {
//        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
//        auto& networkState = registry->ctx().get<Singletons::NetworkState>();
//
//        entt::entity entity;
//        if (!Util::Network::GetEntityIDFromObjectGUID(networkState, packet.guid, entity))
//        {
//            NC_LOG_WARNING("Network : Received Display Info Update for non existing entity ({0})", packet.guid.ToString());
//            return true;
//        }
//
//        if (!registry->valid(entity))
//        {
//            NC_LOG_WARNING("Network : Received Display Info Update for non existing entity ({0})", packet.guid.ToString());
//            return true;
//        }
//
//        ModelLoader* modelLoader = ServiceLocator::GetGameRenderer()->GetModelLoader();
//        auto& model = registry->get<ECS::Components::Model>(entity);
//        auto& displayInfo = registry->get<Components::DisplayInfo>(entity);
//
//        if (displayInfo.displayID != packet.displayID)
//        {
//            displayInfo.displayID = packet.displayID;
//
//            if (!modelLoader->LoadDisplayIDForEntity(entity, model, Database::Unit::DisplayInfoType::Creature, packet.displayID))
//            {
//                NC_LOG_WARNING("Network : Failed to load DisplayID({1}) for entity ({0})", packet.guid.ToString(), packet.displayID);
//
//                modelLoader->LoadDisplayIDForEntity(entity, model, Database::Unit::DisplayInfoType::Creature, 10045);
//                return true;
//            }
//        }
//
//        displayInfo.race = static_cast<GameDefine::UnitRace>(packet.race);
//        displayInfo.gender = static_cast<GameDefine::UnitGender>(packet.gender);
//
//        return true;
//    }
    bool HandleOnUnitEquippedItemUpdate(Network::SocketID socketID, Generated::ServerUnitEquippedItemUpdatePacket& packet)
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
        unitEquipment.dirtyItemIDSlots.insert(static_cast<Generated::ItemEquipSlotEnum>(packet.slot));
        registry->emplace_or_replace<Components::UnitEquipmentDirty>(entity);
        return true;
    }
    bool HandleOnUnitVisualItemUpdate(Network::SocketID socketID, Generated::ServerUnitVisualItemUpdatePacket& packet)
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
        unitEquipment.dirtyVisualItemIDSlots.insert(static_cast<Generated::ItemEquipSlotEnum>(packet.slot));
        registry->emplace_or_replace<Components::UnitVisualEquipmentDirty>(entity);
        return true;
    }

    bool HandleOnUnitPowerUpdate(Network::SocketID socketID, Generated::UnitPowerUpdatePacket& packet)
    {
        auto powerType = static_cast<Generated::PowerTypeEnum>(packet.kind);
        if (powerType <= Generated::PowerTypeEnum::Invalid || powerType >= Generated::PowerTypeEnum::Count)
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
        zenith->CallEvent(Generated::LuaUnitEventEnum::PowerUpdate, Generated::LuaUnitEventDataPowerUpdate{
            .unitID = entt::to_integral(entity),
            .powerType = packet.kind,
            .base = packet.base,
            .current = packet.current,
            .max = packet.max
        });

        return true;
    }
    bool HandleOnUnitResistanceUpdate(Network::SocketID socketID, Generated::UnitResistanceUpdatePacket& packet)
    {
        auto resistanceType = static_cast<Generated::ResistanceTypeEnum>(packet.kind);
        if (resistanceType <= Generated::ResistanceTypeEnum::Invalid || resistanceType >= Generated::ResistanceTypeEnum::Count)
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
        zenith->CallEvent(Generated::LuaUnitEventEnum::ResistanceUpdate, Generated::LuaUnitEventDataResistanceUpdate{
            .unitID = entt::to_integral(entity),
            .resistanceType = packet.kind,
            .base = packet.base,
            .current = packet.current,
            .max = packet.max
        });

        return true;
    }
    bool HandleOnUnitStatUpdate(Network::SocketID socketID, Generated::UnitStatUpdatePacket& packet)
    {
        auto statType = static_cast<Generated::StatTypeEnum>(packet.kind);
        if (statType <= Generated::StatTypeEnum::Invalid || statType >= Generated::StatTypeEnum::Count)
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
        zenith->CallEvent(Generated::LuaUnitEventEnum::StatUpdate, Generated::LuaUnitEventDataStatUpdate{
            .unitID = entt::to_integral(entity),
            .statType = packet.kind,
            .base = packet.base,
            .current = packet.current
        });
        return true;
    }

    bool HandleOnUnitTargetUpdate(Network::SocketID socketID, Generated::ServerUnitTargetUpdatePacket& packet)
    {
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& networkState = registry->ctx().get<Singletons::NetworkState>();

        entt::entity entity;
        if (!Util::Network::GetEntityIDFromObjectGUID(networkState, packet.guid, entity))
        {
            NC_LOG_WARNING("Network : Received Target Update for non existing entity ({0})", packet.guid.ToString());
            return true;
        }

        entt::entity targetEntity;
        if (!Util::Network::GetEntityIDFromObjectGUID(networkState, packet.targetGUID, targetEntity))
        {
            NC_LOG_WARNING("Network : Received Target Update for non existing target entity ({0})", packet.targetGUID.ToString());
            return true;
        }

        auto& unit = registry->get<Components::Unit>(entity);
        unit.targetEntity = targetEntity;

        Scripting::Zenith* zenith = Scripting::Util::Zenith::GetGlobal();
        zenith->CallEvent(Generated::LuaUnitEventEnum::TargetChanged, Generated::LuaUnitEventDataTargetChanged{
            .unitID = entt::to_integral(entity),
            .targetID = entt::to_integral(targetEntity)
        });

        return true;
    }
    bool HandleOnUnitCastSpell(Network::SocketID socketID, Generated::UnitCastSpellPacket& packet)
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

        if (packet.spellID == 1)
        {
            auto& characterSingleton = registry->ctx().get<Singletons::CharacterSingleton>();

            entt::registry* dbRegistry = ServiceLocator::GetEnttRegistries()->dbRegistry;
            auto& clientDBSingleton = dbRegistry->ctx().get<Singletons::ClientDBSingleton>();
            auto& itemSingleton = dbRegistry->ctx().get<Singletons::ItemSingleton>();

            auto* itemStorage = clientDBSingleton.Get(ClientDBHash::Item);

            auto& unitEquipment = registry->get<Components::UnitEquipment>(entity);
            u32 mainHandItemID = unitEquipment.equipmentSlotToItemID[static_cast<u32>(Generated::ItemEquipSlotEnum::MainHand)];
            auto& itemTemplate = itemStorage->Get<Generated::ItemRecord>(mainHandItemID);

            if (characterSingleton.moverEntity == entity)
            {
                u32 itemWeaponTemplateID = ::ECSUtil::Item::GetItemWeaponTemplateID(itemSingleton, mainHandItemID);
                auto* itemWeaponTemplateStorage = clientDBSingleton.Get(ClientDBHash::ItemWeaponTemplate);
                auto& itemWeaponTemplate = itemWeaponTemplateStorage->Get<Generated::ItemWeaponTemplateRecord>(itemWeaponTemplateID);
                
                characterSingleton.primaryAttackTimer = itemWeaponTemplate.speed;
            }

            if (auto* model = registry->try_get<Components::Model>(entity))
            {
                if (auto* modelInfo = ServiceLocator::GetGameRenderer()->GetModelLoader()->GetModelInfo(model->modelHash))
                {
                    auto& animationData = registry->get<Components::AnimationData>(entity);

                    unit->attackReadyAnimation = ::Util::Unit::GetAttackReadyAnimation(itemTemplate.categoryType);
                    unit->attackMainHandAnimation = ::Util::Unit::GetMainHandAttackAnimation(itemTemplate.categoryType);
                }
            }
        }
        else if (packet.spellID == 2)
        {
            auto& characterSingleton = registry->ctx().get<Singletons::CharacterSingleton>();

            entt::registry* dbRegistry = ServiceLocator::GetEnttRegistries()->dbRegistry;
            auto& clientDBSingleton = dbRegistry->ctx().get<Singletons::ClientDBSingleton>();
            auto& itemSingleton = dbRegistry->ctx().get<Singletons::ItemSingleton>();

            auto* itemStorage = clientDBSingleton.Get(ClientDBHash::Item);

            auto& unitEquipment = registry->get<Components::UnitEquipment>(entity);
            u32 offHandItemID = unitEquipment.equipmentSlotToItemID[static_cast<u32>(Generated::ItemEquipSlotEnum::OffHand)];
            auto& itemTemplate = itemStorage->Get<Generated::ItemRecord>(offHandItemID);

            if (characterSingleton.moverEntity == entity)
            {
                u32 itemWeaponTemplateID = ::ECSUtil::Item::GetItemWeaponTemplateID(itemSingleton, offHandItemID);
                auto* itemWeaponTemplateStorage = clientDBSingleton.Get(ClientDBHash::ItemWeaponTemplate);
                auto& itemWeaponTemplate = itemWeaponTemplateStorage->Get<Generated::ItemWeaponTemplateRecord>(itemWeaponTemplateID);
                
                characterSingleton.secondaryAttackTimer = itemWeaponTemplate.speed;
            }


            if (auto* model = registry->try_get<Components::Model>(entity))
            {
                if (auto* modelInfo = ServiceLocator::GetGameRenderer()->GetModelLoader()->GetModelInfo(model->modelHash))
                {
                    auto& animationData = registry->get<Components::AnimationData>(entity);

                    unit->attackReadyAnimation = ::Util::Unit::GetAttackReadyAnimation(itemTemplate.categoryType);
                    unit->attackMainHandAnimation = ::Util::Unit::GetOffHandAttackAnimation(itemTemplate.categoryType);
                }
            }
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
    bool HandleOnUnitSetMover(Network::SocketID socketID, Generated::UnitSetMoverPacket& packet)
    {
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& networkState = registry->ctx().get<Singletons::NetworkState>();

        entt::entity entity;
        if (!Util::Network::GetEntityIDFromObjectGUID(networkState, packet.guid, entity))
        {
            NC_LOG_WARNING("Network : Received UnitSetMover for non-existent entity ({0})", packet.guid.ToString());
            return true;
        }

        auto& characterSingleton = registry->ctx().get<Singletons::CharacterSingleton>();
        characterSingleton.moverEntity = entity;

        CharacterController::InitCharacterController(*registry, false);

        Scripting::Zenith* zenith = Scripting::Util::Zenith::GetGlobal();
        zenith->CallEvent(Generated::LuaGameEventEnum::LocalMoverChanged, Generated::LuaGameEventDataLocalMoverChanged{
            .moverID = entt::to_integral(entity)
        });

        return true;
    }
    bool HandleOnUnitMove(Network::SocketID socketID, Generated::ServerUnitMovePacket& packet)
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

        unitMovementOverTime.startPos = transform.GetWorldPosition();
        unitMovementOverTime.endPos = packet.position;
        unitMovementOverTime.time = 0.0f;

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

        return true;
    }
    bool HandleOnUnitMoveStop(Network::SocketID socketID, Generated::UnitMoveStopPacket& packet)
    {
        return true;
    }
    bool HandleOnUnitTeleport(Network::SocketID socketID, Generated::ServerUnitTeleportPacket& packet)
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

        f32 scale = transform.GetLocalScale().x;
        vec3 min = packet.position - (scale * 0.5f);
        vec3 max = packet.position + (scale * 0.5f);
        if (auto* aabb = registry->try_get<Components::AABB>(entity))
        {
            min = packet.position + (aabb->centerPos - aabb->extents);
            max = packet.position + (aabb->centerPos + aabb->extents);
        }
        networkState.networkVisTree->Remove(packet.guid);
        networkState.networkVisTree->Insert(&min.x, &max.x, packet.guid);

        if (auto* unitMovementOverTime = registry->try_get<Components::UnitMovementOverTime>(entity))
        {
            unitMovementOverTime->startPos = packet.position;
            unitMovementOverTime->endPos = packet.position;
            unitMovementOverTime->time = 1.0f;
        }

        return true;
    }

    bool HandleOnItemAdd(Network::SocketID socketID, Generated::ItemAddPacket& packet)
    {
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& networkState = registry->ctx().get<Singletons::NetworkState>();

        if (Util::Network::IsObjectGUIDKnown(networkState, packet.guid))
        {
            NC_LOG_WARNING("Network : Received ItemAdd for already existing item ({0})", packet.guid.ToString());
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

    bool HandleOnContainerAdd(Network::SocketID socketID, Generated::ContainerAddPacket& packet)
    {
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& networkState = registry->ctx().get<Singletons::NetworkState>();

        if (Util::Network::IsObjectGUIDKnown(networkState, packet.guid))
        {
            NC_LOG_WARNING("Network : Received ContainerAdd for already existing container ({0})", packet.guid.ToString());
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
        zenith->CallEvent(Generated::LuaContainerEventEnum::Add, Generated::LuaContainerEventDataAdd{
            .index = packet.index + 1u,
            .numSlots = container.numSlots,
            .itemID = container.itemID
        });
        return true;
    }
    bool HandleOnContainerAddToSlot(Network::SocketID socketID, Generated::ContainerAddToSlotPacket& packet)
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
        zenith->CallEvent(Generated::LuaContainerEventEnum::AddToSlot, Generated::LuaContainerEventDataAddToSlot{
            .containerIndex = packet.index + 1u,
            .slotIndex = packet.slot + 1u,
            .itemID = item.itemID,
            .count = item.count
        });

        return true;
    }
    bool HandleOnContainerRemoveFromSlot(Network::SocketID socketID, Generated::ContainerRemoveFromSlotPacket& packet)
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
        zenith->CallEvent(Generated::LuaContainerEventEnum::RemoveFromSlot, Generated::LuaContainerEventDataRemoveFromSlot{
            .containerIndex = packet.index + 1u,
            .slotIndex = packet.slot + 1u
        });

        return true;
    }
    bool HandleOnContainerSwapSlots(Network::SocketID socketID, Generated::ServerContainerSwapSlotsPacket& packet)
    {
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& characterSingleton = registry->ctx().get<Singletons::CharacterSingleton>();
        auto& networkState = registry->ctx().get<Singletons::NetworkState>();

        Components::Container* srcContainer = nullptr;
        Components::Container* dstContainer = nullptr;

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

        std::swap(srcContainer->items[packet.srcSlot], dstContainer->items[packet.dstSlot]);

        if (auto* unitEquipment = registry->try_get<Components::UnitEquipment>(characterSingleton.moverEntity))
        {
            if (packet.srcContainer == 0)
            {
                auto equippedSlot = static_cast<Generated::ItemEquipSlotEnum>(packet.srcSlot);
                if (equippedSlot >= Generated::ItemEquipSlotEnum::EquipmentStart && equippedSlot <= Generated::ItemEquipSlotEnum::EquipmentEnd)
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
                auto equippedSlot = static_cast<Generated::ItemEquipSlotEnum>(packet.dstSlot);
                if (equippedSlot >= Generated::ItemEquipSlotEnum::EquipmentStart && equippedSlot <= Generated::ItemEquipSlotEnum::EquipmentEnd)
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
        zenith->CallEvent(Generated::LuaContainerEventEnum::SwapSlots, Generated::LuaContainerEventDataSwapSlots{
            .srcContainerIndex = packet.srcContainer + 1u,
            .destContainerIndex = packet.dstContainer + 1u,
            .srcSlotIndex = packet.srcSlot + 1u,
            .destSlotIndex = packet.dstSlot + 1u
        });

        return true;
    }

    bool HandleOnServerSpellCastResult(Network::SocketID socketID, Generated::ServerSpellCastResultPacket& packet)
    {
        if (packet.result == 0)
            return true;


        // TODO : Report spell cast failure to player
        return true;
    }

    bool HandleOnSendChatMessage(Network::SocketID socketID, Generated::ServerSendChatMessagePacket& packet)
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
        zenith->CallEvent(Generated::LuaGameEventEnum::ChatMessageReceived, Generated::LuaGameEventDataChatMessageReceived{
            .sender = senderName,
            .channel = channel,
            .message = packet.message
        });

        return true;
    }

    bool HandleOnServerTriggerAdd(Network::SocketID socketID, Generated::ServerTriggerAddPacket& packet)
    {
        entt::registry& registry = *ServiceLocator::GetEnttRegistries()->gameRegistry;

        auto flags = static_cast<Generated::ProximityTriggerFlagEnum>(packet.flags);
        ECS::Util::ProximityTriggerUtil::CreateTrigger(registry, packet.triggerID, packet.name, flags, packet.mapID, packet.position, packet.extents);
        return true;
    }

    bool HandleOnServerTriggerRemove(Network::SocketID socketID, Generated::ServerTriggerRemovePacket& packet)
    {
        entt::registry& registry = *ServiceLocator::GetEnttRegistries()->gameRegistry;
        ECS::Util::ProximityTriggerUtil::DestroyTrigger(registry, packet.triggerID);
        return true;
    }

    bool HandleOnServerUnitAddAura(Network::SocketID socketID, Generated::ServerUnitAddAuraPacket& packet)
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

        // Calculate aura expiration timestamp
        u64 currentTime = static_cast<u64>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
        u64 expirationTime = currentTime + static_cast<u64>(packet.duration * 1000.0f);

        u32 auraIndex = static_cast<u32>(unitAuraInfo.auras.size());
        auto& auraInfo = unitAuraInfo.auras.emplace_back();
        auraInfo.unitID = entt::to_integral(unitID);
        auraInfo.auraID = packet.auraInstanceID;
        auraInfo.spellID = packet.spellID;
        auraInfo.expireTimestamp = expirationTime;
        auraInfo.stacks = packet.stacks;

        unitAuraInfo.auraIDToAuraIndex[packet.auraInstanceID] = auraIndex;
        unitAuraInfo.spellIDToAuraIndex[packet.spellID] = auraIndex;

        Scripting::Zenith* zenith = Scripting::Util::Zenith::GetGlobal();
        zenith->CallEvent(Generated::LuaUnitEventEnum::AuraAdd, Generated::LuaUnitEventDataAuraAdd{
            .unitID = entt::to_integral(unitID),
            .auraID = packet.auraInstanceID,
            .spellID = packet.spellID,
            .duration = packet.duration,
            .stacks = packet.stacks
        });

        return true;
    }

    bool HandleOnServerUnitUpdateAura(Network::SocketID socketID, Generated::ServerUnitUpdateAuraPacket& packet)
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
        u64 currentTime = static_cast<u64>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
        u64 expirationTime = currentTime + static_cast<u64>(packet.duration * 1000.0f);

        auraInfo.expireTimestamp = expirationTime;
        auraInfo.stacks = packet.stacks;

        Scripting::Zenith* zenith = Scripting::Util::Zenith::GetGlobal();
        zenith->CallEvent(Generated::LuaUnitEventEnum::AuraUpdate, Generated::LuaUnitEventDataAuraUpdate{
            .unitID = entt::to_integral(unitID),
            .auraID = packet.auraInstanceID,
            .duration = packet.duration,
            .stacks = packet.stacks
        });

        return true;
    }

    bool HandleOnServerUnitRemoveAura(Network::SocketID socketID, Generated::ServerUnitRemoveAuraPacket& packet)
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
        zenith->CallEvent(Generated::LuaUnitEventEnum::AuraRemove, Generated::LuaUnitEventDataAuraRemove{
            .unitID = entt::to_integral(unitID),
            .auraID = packet.auraInstanceID
        });

        u32 auraIndex = unitAuraInfo.auraIDToAuraIndex[packet.auraInstanceID];
        u32 auraSpellID = unitAuraInfo.auras[auraIndex].spellID;
        unitAuraInfo.auras.erase(unitAuraInfo.auras.begin() + auraIndex);

        // Rebuild the auraIDToAuraIndex and spellIDToAuraIndex maps
        unitAuraInfo.auraIDToAuraIndex.clear();
        unitAuraInfo.spellIDToAuraIndex.clear();

        for (u32 i = 0; i < unitAuraInfo.auras.size(); ++i)
        {
            unitAuraInfo.auraIDToAuraIndex[unitAuraInfo.auras[i].auraID] = i;
            unitAuraInfo.spellIDToAuraIndex[unitAuraInfo.auras[i].spellID] = i;
        }

        return true;
    }

    void NetworkConnection::Init(entt::registry& registry)
    {
        entt::registry::context& ctx = registry.ctx();

        auto& networkState = ctx.emplace<Singletons::NetworkState>();

        // Setup NetworkState
        {
            networkState.resolver = std::make_shared<asio::ip::tcp::resolver>(networkState.asioContext);
            networkState.client = std::make_unique<Network::Client>(networkState.asioContext, networkState.resolver);
            networkState.networkIDToEntity.reserve(1024);
            networkState.entityToNetworkID.reserve(1024);
            networkState.networkVisTree = new RTree<ObjectGUID, f32, 3>();
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

            networkState.gameMessageRouter->RegisterPacketHandler(Network::ConnectionStatus::Connected, HandleOnUnitAdd);
            networkState.gameMessageRouter->RegisterPacketHandler(Network::ConnectionStatus::Connected, HandleOnUnitRemove);
            //networkState.gameMessageRouter->RegisterPacketHandler(Network::ConnectionStatus::Connected, HandleOnUnitDisplayInfoUpdate);
            networkState.gameMessageRouter->RegisterPacketHandler(Network::ConnectionStatus::Connected, HandleOnUnitEquippedItemUpdate);
            networkState.gameMessageRouter->RegisterPacketHandler(Network::ConnectionStatus::Connected, HandleOnUnitVisualItemUpdate);
            networkState.gameMessageRouter->RegisterPacketHandler(Network::ConnectionStatus::Connected, HandleOnUnitPowerUpdate);
            networkState.gameMessageRouter->RegisterPacketHandler(Network::ConnectionStatus::Connected, HandleOnUnitResistanceUpdate);
            networkState.gameMessageRouter->RegisterPacketHandler(Network::ConnectionStatus::Connected, HandleOnUnitStatUpdate);
            networkState.gameMessageRouter->RegisterPacketHandler(Network::ConnectionStatus::Connected, HandleOnUnitTargetUpdate);
            networkState.gameMessageRouter->RegisterPacketHandler(Network::ConnectionStatus::Connected, HandleOnUnitCastSpell);
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

            networkState.gameMessageRouter->SetMessageHandler(Generated::ServerCharacterListPacket::PACKET_ID, Network::GameMessageHandler(Network::ConnectionStatus::Connected, 0u, -1, &HandleOnCharacterList));
            networkState.gameMessageRouter->SetMessageHandler(Generated::SendCombatEventPacket::PACKET_ID, Network::GameMessageHandler(Network::ConnectionStatus::Connected, 0u, -1, &HandleOnCombatEvent));
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
                CharacterController::DeleteCharacterController(registry, true);
            }
            else
            {
                u64 timeDiff = currentTime - networkState.pingInfo.lastPingTime;
                if (timeDiff >= Singletons::NetworkState::PING_INTERVAL)
                {
                    std::shared_ptr<Bytebuffer> buffer = Bytebuffer::Borrow<16>();
                    if (Util::Network::SendPacket(networkState, Generated::PingPacket{
                        .ping = networkState.pingInfo.ping
                    }))
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

                CharacterController::DeleteCharacterController(registry, false);

                for (auto& [entity, networkID] : networkState.entityToNetworkID)
                {
                    if (!registry.valid(entity))
                        continue;

                    if (auto* attachmentData = registry.try_get<Components::AttachmentData>(entity))
                    {
                        for (auto& pair : attachmentData->attachmentToInstance)
                        {
                            ::Util::Unit::RemoveItemFromAttachment(registry, entity, pair.first);
                        }
                    }

                    if (auto* model = registry.try_get<Components::Model>(entity))
                    {
                        if (model->instanceID != std::numeric_limits<u32>().max())
                        {
                            ModelLoader* modelLoader = ServiceLocator::GetGameRenderer()->GetModelLoader();
                            modelLoader->UnloadModelForEntity(entity, *model);
                        }
                    }

                    registry.destroy(entity);
                }

                // Clean up any networked proximity triggers
                auto& proximityTriggerSingleton = ctx.get<Singletons::ProximityTriggerSingleton>();
                auto triggerView = registry.view<Components::ProximityTrigger>();
                triggerView.each([&](entt::entity triggerEntity, Components::ProximityTrigger& proximityTrigger)
                {
                    if (proximityTrigger.networkID == Components::ProximityTrigger::INVALID_NETWORK_ID)
                        return;

                    proximityTriggerSingleton.proximityTriggers.Remove(triggerEntity);
                });

                networkState.isLoadingMap = false;
                networkState.isInWorld = false;

                networkState.authInfo.Reset();
                networkState.characterListInfo.Reset();
                networkState.pingInfo.Reset();
                networkState.entityToNetworkID.clear();
                networkState.networkIDToEntity.clear();
                networkState.networkVisTree->RemoveAll();

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