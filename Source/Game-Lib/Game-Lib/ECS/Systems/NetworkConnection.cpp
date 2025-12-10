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
#include <MetaGen/Shared/CombatLog/CombatLog.h>
#include <MetaGen/Shared/Packet/Packet.h>
#include <MetaGen/Shared/Unit/Unit.h>

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

                std::string result = "";

                if (eventID == MetaGen::Shared::CombatLog::CombatLogEventEnum::DamageDealt)
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
        const std::string& mapInternalName = mapStorage->GetString(map.nameInternal);

        u32 internalMapNameHash = StringUtils::fnv1a_32(mapInternalName.c_str(), mapInternalName.length());
        mapLoader->LoadMap(internalMapNameHash);
        networkState.isLoadingMap = true;
        return true;
    }
    bool HandleOnCharacterLogout(Network::SocketID socketID, MetaGen::Shared::Packet::ServerCharacterLogoutPacket& packet)
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

        vec3 min = packet.position - (packet.scale * 0.5f);
        vec3 max = packet.position + (packet.scale * 0.5f);
        if (auto* aabb = registry->try_get<Components::AABB>(newEntity))
        {
            min = packet.position + (aabb->centerPos - aabb->extents);
            max = packet.position + (aabb->centerPos + aabb->extents);
        }
        networkState.networkVisTree->Insert(&min.x, &max.x, packet.guid);

        Scripting::Zenith* zenith = Scripting::Util::Zenith::GetGlobal();
        zenith->CallEvent(MetaGen::Game::Lua::UnitEvent::Add, MetaGen::Game::Lua::UnitEventDataAdd{
            .unitID = entt::to_integral(newEntity)
        });

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

        registry->destroy(entity);

        Scripting::Zenith* zenith = Scripting::Util::Zenith::GetGlobal();
        zenith->CallEvent(MetaGen::Game::Lua::UnitEvent::Remove, MetaGen::Game::Lua::UnitEventDataRemove{
            .unitID = entt::to_integral(entity)
        });

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
        zenith->CallEvent(MetaGen::Game::Lua::UnitEvent::PowerUpdate, MetaGen::Game::Lua::UnitEventDataPowerUpdate{
            .unitID = entt::to_integral(entity),
            .powerType = packet.kind,
            .base = packet.base,
            .current = packet.current,
            .max = packet.max
        });

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
        zenith->CallEvent(MetaGen::Game::Lua::UnitEvent::ResistanceUpdate, MetaGen::Game::Lua::UnitEventDataResistanceUpdate{
            .unitID = entt::to_integral(entity),
            .resistanceType = packet.kind,
            .base = packet.base,
            .current = packet.current,
            .max = packet.max
        });

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
        zenith->CallEvent(MetaGen::Game::Lua::UnitEvent::StatUpdate, MetaGen::Game::Lua::UnitEventDataStatUpdate{
            .unitID = entt::to_integral(entity),
            .statType = packet.kind,
            .base = packet.base,
            .current = packet.current
        });
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

        entt::entity targetEntity;
        if (!Util::Network::GetEntityIDFromObjectGUID(networkState, packet.targetGUID, targetEntity))
        {
            NC_LOG_WARNING("Network : Received Target Update for non existing target entity ({0})", packet.targetGUID.ToString());
            return true;
        }

        auto& unit = registry->get<Components::Unit>(entity);
        unit.targetEntity = targetEntity;

        Scripting::Zenith* zenith = Scripting::Util::Zenith::GetGlobal();
        zenith->CallEvent(MetaGen::Game::Lua::UnitEvent::TargetChanged, MetaGen::Game::Lua::UnitEventDataTargetChanged{
            .unitID = entt::to_integral(entity),
            .targetID = entt::to_integral(targetEntity)
        });

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

        if (packet.spellID == 1)
        {
            auto& characterSingleton = registry->ctx().get<Singletons::CharacterSingleton>();

            entt::registry* dbRegistry = ServiceLocator::GetEnttRegistries()->dbRegistry;
            auto& clientDBSingleton = dbRegistry->ctx().get<Singletons::ClientDBSingleton>();
            auto& itemSingleton = dbRegistry->ctx().get<Singletons::ItemSingleton>();

            auto* itemStorage = clientDBSingleton.Get(ClientDBHash::Item);

            auto& unitEquipment = registry->get<Components::UnitEquipment>(entity);
            u32 mainHandItemID = unitEquipment.equipmentSlotToItemID[static_cast<u32>(MetaGen::Shared::Unit::ItemEquipSlotEnum::MainHand)];
            auto& itemTemplate = itemStorage->Get<MetaGen::Shared::ClientDB::ItemRecord>(mainHandItemID);

            if (characterSingleton.moverEntity == entity)
            {
                u32 itemWeaponTemplateID = ::ECSUtil::Item::GetItemWeaponTemplateID(itemSingleton, mainHandItemID);
                auto* itemWeaponTemplateStorage = clientDBSingleton.Get(ClientDBHash::ItemWeaponTemplate);
                auto& itemWeaponTemplate = itemWeaponTemplateStorage->Get<MetaGen::Shared::ClientDB::ItemWeaponTemplateRecord>(itemWeaponTemplateID);
                
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
            u32 offHandItemID = unitEquipment.equipmentSlotToItemID[static_cast<u32>(MetaGen::Shared::Unit::ItemEquipSlotEnum::OffHand)];
            auto& itemTemplate = itemStorage->Get<MetaGen::Shared::ClientDB::ItemRecord>(offHandItemID);

            if (characterSingleton.moverEntity == entity)
            {
                u32 itemWeaponTemplateID = ::ECSUtil::Item::GetItemWeaponTemplateID(itemSingleton, offHandItemID);
                auto* itemWeaponTemplateStorage = clientDBSingleton.Get(ClientDBHash::ItemWeaponTemplate);
                auto& itemWeaponTemplate = itemWeaponTemplateStorage->Get<MetaGen::Shared::ClientDB::ItemWeaponTemplateRecord>(itemWeaponTemplateID);
                
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

        CharacterController::InitCharacterController(*registry, false);

        Scripting::Zenith* zenith = Scripting::Util::Zenith::GetGlobal();
        zenith->CallEvent(MetaGen::Game::Lua::GameEvent::LocalMoverChanged, MetaGen::Game::Lua::GameEventDataLocalMoverChanged{
            .moverID = entt::to_integral(entity)
        });

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
        zenith->CallEvent(MetaGen::Game::Lua::ContainerEvent::Add, MetaGen::Game::Lua::ContainerEventDataAdd{
            .index = packet.index + 1u,
            .numSlots = container.numSlots,
            .itemID = container.itemID
        });
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
        zenith->CallEvent(MetaGen::Game::Lua::ContainerEvent::AddToSlot, MetaGen::Game::Lua::ContainerEventDataAddToSlot{
            .containerIndex = packet.index + 1u,
            .slotIndex = packet.slot + 1u,
            .itemID = item.itemID,
            .count = item.count
        });

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
        zenith->CallEvent(MetaGen::Game::Lua::ContainerEvent::RemoveFromSlot, MetaGen::Game::Lua::ContainerEventDataRemoveFromSlot{
            .containerIndex = packet.index + 1u,
            .slotIndex = packet.slot + 1u
        });

        return true;
    }
    bool HandleOnContainerSwapSlots(Network::SocketID socketID, MetaGen::Shared::Packet::SharedContainerSwapSlotsPacket& packet)
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
        zenith->CallEvent(MetaGen::Game::Lua::ContainerEvent::SwapSlots, MetaGen::Game::Lua::ContainerEventDataSwapSlots{
            .srcContainerIndex = packet.srcContainer + 1u,
            .destContainerIndex = packet.dstContainer + 1u,
            .srcSlotIndex = packet.srcSlot + 1u,
            .destSlotIndex = packet.dstSlot + 1u
        });

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
        zenith->CallEvent(MetaGen::Game::Lua::GameEvent::ChatMessageReceived, MetaGen::Game::Lua::GameEventDataChatMessageReceived{
            .sender = senderName,
            .channel = channel,
            .message = packet.message
        });

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
        zenith->CallEvent(MetaGen::Game::Lua::UnitEvent::AuraAdd, MetaGen::Game::Lua::UnitEventDataAuraAdd{
            .unitID = entt::to_integral(unitID),
            .auraID = packet.auraInstanceID,
            .spellID = packet.spellID,
            .duration = packet.duration,
            .stacks = packet.stacks
        });

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
        u64 currentTime = static_cast<u64>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
        u64 expirationTime = currentTime + static_cast<u64>(packet.duration * 1000.0f);

        auraInfo.expireTimestamp = expirationTime;
        auraInfo.stacks = packet.stacks;

        Scripting::Zenith* zenith = Scripting::Util::Zenith::GetGlobal();
        zenith->CallEvent(MetaGen::Game::Lua::UnitEvent::AuraUpdate, MetaGen::Game::Lua::UnitEventDataAuraUpdate{
            .unitID = entt::to_integral(unitID),
            .auraID = packet.auraInstanceID,
            .duration = packet.duration,
            .stacks = packet.stacks
        });

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
        zenith->CallEvent(MetaGen::Game::Lua::UnitEvent::AuraRemove, MetaGen::Game::Lua::UnitEventDataAuraRemove{
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

            networkState.gameMessageRouter->SetMessageHandler(MetaGen::Shared::Packet::ServerCharacterListPacket::PACKET_ID, Network::GameMessageHandler(Network::ConnectionStatus::Connected, 0u, -1, &HandleOnCharacterList));
            networkState.gameMessageRouter->SetMessageHandler(MetaGen::Shared::Packet::ServerObjectNetFieldUpdatePacket::PACKET_ID, Network::GameMessageHandler(Network::ConnectionStatus::Connected, 0u, -1, &HandleOnObjectNetFieldUpdate));
            networkState.gameMessageRouter->SetMessageHandler(MetaGen::Shared::Packet::ServerUnitNetFieldUpdatePacket::PACKET_ID, Network::GameMessageHandler(Network::ConnectionStatus::Connected, 0u, -1, &HandleOnUnitNetFieldUpdate));
            networkState.gameMessageRouter->SetMessageHandler(MetaGen::Shared::Packet::ServerSendCombatEventPacket::PACKET_ID, Network::GameMessageHandler(Network::ConnectionStatus::Connected, 0u, -1, &HandleOnCombatEvent));
            networkState.gameMessageRouter->SetMessageHandler(MetaGen::Shared::Packet::ServerPathVisualizationPacket::PACKET_ID, Network::GameMessageHandler(Network::ConnectionStatus::Connected, 0u, -1, &HandleOnVisualizePath));

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
                CharacterController::DeleteCharacterController(registry, true);
            }
            else
            {
                u64 timeDiff = currentTime - networkState.pingInfo.lastPingTime;
                if (timeDiff >= Singletons::NetworkState::PING_INTERVAL)
                {
                    std::shared_ptr<Bytebuffer> buffer = Bytebuffer::Borrow<16>();
                    if (Util::Network::SendPacket(networkState, MetaGen::Shared::Packet::ClientPingPacket{
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
                networkState.pathToVisualize.clear();
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