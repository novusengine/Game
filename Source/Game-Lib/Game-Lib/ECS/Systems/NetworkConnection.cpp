#include "NetworkConnection.h"
#include "CharacterController.h"

#include "Game-Lib/ECS/Components/AABB.h"
#include "Game-Lib/ECS/Components/AnimationData.h"
#include "Game-Lib/ECS/Components/AttachmentData.h"
#include "Game-Lib/ECS/Components/CastInfo.h"
#include "Game-Lib/ECS/Components/Container.h"
#include "Game-Lib/ECS/Components/DisplayInfo.h"
#include "Game-Lib/ECS/Components/Model.h"
#include "Game-Lib/ECS/Components/MovementInfo.h"
#include "Game-Lib/ECS/Components/Name.h"
#include "Game-Lib/ECS/Components/Item.h"
#include "Game-Lib/ECS/Components/ProximityTrigger.h"
#include "Game-Lib/ECS/Components/Tags.h"
#include "Game-Lib/ECS/Components/Unit.h"
#include "Game-Lib/ECS/Components/UnitCustomization.h"
#include "Game-Lib/ECS/Components/UnitEquipment.h"
#include "Game-Lib/ECS/Components/UnitMovementOverTime.h"
#include "Game-Lib/ECS/Components/UnitStatsComponent.h"
#include "Game-Lib/ECS/Singletons/CharacterSingleton.h"
#include "Game-Lib/ECS/Singletons/NetworkState.h"
#include "Game-Lib/ECS/Singletons/ProximityTriggerSingleton.h"
#include "Game-Lib/ECS/Singletons/Database/ClientDBSingleton.h"
#include "Game-Lib/ECS/Util/MessageBuilderUtil.h"
#include "Game-Lib/ECS/Util/ProximityTriggerUtil.h"
#include "Game-Lib/ECS/Util/Transforms.h"
#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Rendering/Model/ModelLoader.h"
#include "Game-Lib/Scripting/LuaManager.h"
#include "Game-Lib/Scripting/Handlers/PlayerEventHandler.h"
#include "Game-Lib/Util/ServiceLocator.h"
#include "Game-Lib/Util/UnitUtil.h"

#include <Base/CVarSystem/CVarSystem.h>
#include <Base/Util/DebugHandler.h>

#include <Gameplay/Network/GameMessageRouter.h>

#include <Meta/Generated/Game/ProximityTriggerEnum.h>

#include <Network/Client.h>
#include <Network/Define.h>

#include <entt/entt.hpp>
#include <imgui/ImGuiNotify.hpp>

#include <chrono>
#include <numeric>

namespace ECS::Systems
{
    bool HandleOnConnected(Network::SocketID socketID, Network::Message& message)
    {
        Network::ConnectResult result = Network::ConnectResult::None;
        if (!message.buffer->Get(result))
            return false;

        if (result != Network::ConnectResult::Success)
        {
            NC_LOG_WARNING("Network : Failed to login to character");
            return false;
        }

        NC_LOG_INFO("Network : Logged in to character");
        return true;
    }

    bool HandleOnPong(Network::SocketID socketID, Network::Message& message)
    {
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& networkState = registry->ctx().get<Singletons::NetworkState>();

        auto currentTime = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        f64 rtt = static_cast<f64>(currentTime) - static_cast<f64>(networkState.lastPingTime);
        u16 ping = static_cast<u16>(rtt / 2.0f);

        networkState.lastPongTime = currentTime;
        
        u8 pingHistoryCounter = networkState.pingHistoryIndex + 1;
        if (pingHistoryCounter == networkState.pingHistory.size())
            pingHistoryCounter = 0;

        networkState.pingHistorySize = glm::min(static_cast<u8>(networkState.pingHistorySize + 1u), static_cast<u8>(networkState.pingHistory.size()));

        networkState.pingHistoryIndex = pingHistoryCounter;
        networkState.pingHistory[pingHistoryCounter] = ping;

        f32 accumulatedPing = 0.0f;
        for (u16 ping : networkState.pingHistory)
            accumulatedPing += static_cast<f32>(ping);

        accumulatedPing /= networkState.pingHistorySize;
        networkState.ping = static_cast<u16>(glm::round(accumulatedPing));

        return true;
    }

    bool HandleOnUpdateStats(Network::SocketID socketID, Network::Message& message)
    {
        u8 serverUpdateDiff = 0;
        if (!message.buffer->Get(serverUpdateDiff))
            return false;

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& networkState = registry->ctx().get<Singletons::NetworkState>();

        networkState.serverUpdateDiff = serverUpdateDiff;

        return true;
    }

    bool HandleOnCheatCommandResult(Network::SocketID socketID, Network::Message& message)
    {
        u8 result = 0;
        std::string resultText = "";

        if (!message.buffer->GetU8(result))
            return false;

        if (!message.buffer->GetString(resultText))
            return false;

        if (result != 0)
        {
            NC_LOG_WARNING("Network : ({0})", resultText);
            return true;
        }
        else
        {
            NC_LOG_INFO("Network : ({0})", resultText);
            return true;
        }
    }

    bool HandleOnSetMover(Network::SocketID socketID, Network::Message& message)
    {
        GameDefine::ObjectGuid networkID;
        if (!message.buffer->Deserialize(networkID))
            return false;

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& networkState = registry->ctx().get<Singletons::NetworkState>();

        if (!networkState.networkIDToEntity.contains(networkID))
        {
            NC_LOG_WARNING("Network : Received SetMover for non-existent entity ({0})", networkID.ToString());
            return true;
        }

        auto& characterSingleton = registry->ctx().get<Singletons::CharacterSingleton>();
        entt::entity entity = networkState.networkIDToEntity[networkID];

        characterSingleton.moverEntity = entity;
        CharacterController::InitCharacterController(*registry, false);

        return true;
    }
    bool HandleOnEntityCreate(Network::SocketID socketID, Network::Message& message)
    {
        GameDefine::ObjectGuid networkID;
        vec3 position = vec3(0.0f);
        quat rotation = quat(1.0f, 0.0f, 0.0f, 0.0f);
        vec3 scale = vec3(1.0f);

        if (!message.buffer->Deserialize(networkID))
            return false;

        if (!message.buffer->Get(position))
            return false;

        if (!message.buffer->Get(rotation))
            return false;

        if (!message.buffer->Get(scale))
            return false;

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& networkState = registry->ctx().get<Singletons::NetworkState>();

        if (networkState.networkIDToEntity.contains(networkID))
        {
            NC_LOG_WARNING("Network : Received Create Entity for already existing entity ({0})", networkID.ToString());
            return true;
        }

        entt::entity newEntity = registry->create();
        registry->emplace<Components::AABB>(newEntity);
        registry->emplace<Components::WorldAABB>(newEntity);
        registry->emplace<Components::Transform>(newEntity);
        registry->emplace<Components::Name>(newEntity);
        registry->emplace<Components::Model>(newEntity);
        registry->emplace<Components::MovementInfo>(newEntity);
        registry->emplace<Components::UnitCustomization>(newEntity);
        registry->emplace<Components::UnitEquipment>(newEntity);
        registry->emplace<Components::UnitMovementOverTime>(newEntity);
        registry->emplace<Components::UnitStatsComponent>(newEntity);
        registry->emplace<Components::AttachmentData>(newEntity);
        auto& displayInfo = registry->emplace<Components::DisplayInfo>(newEntity);
        displayInfo.displayID = 0;

        auto& unit = registry->emplace<Components::Unit>(newEntity);
        unit.networkID = networkID;
        unit.targetEntity = entt::null;

        if (unit.networkID.GetType() == GameDefine::ObjectGuid::Type::Player)
        {
            registry->emplace_or_replace<Components::PlayerTag>(newEntity);
        }

        TransformSystem& transformSystem = TransformSystem::Get(*registry);
        transformSystem.SetWorldPosition(newEntity, position);
        transformSystem.SetWorldRotation(newEntity, rotation);
        transformSystem.SetLocalScale(newEntity, scale);

        networkState.networkIDToEntity[networkID] = newEntity;
        networkState.entityToNetworkID[newEntity] = networkID;

        return true;
    }
    bool HandleOnEntityDestroy(Network::SocketID socketID, Network::Message& message)
    {
        GameDefine::ObjectGuid networkID;
        if (!message.buffer->Deserialize(networkID))
            return false;

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& networkState = registry->ctx().get<Singletons::NetworkState>();

        if (!networkState.networkIDToEntity.contains(networkID))
        {
            NC_LOG_WARNING("Network : Received Delete Entity for unknown entity ({0})", networkID.ToString());
            return true;
        }

        entt::entity entity = networkState.networkIDToEntity[networkID];

        if (registry->any_of<Components::Model>(entity))
        {
            ModelLoader* modelLoader = ServiceLocator::GetGameRenderer()->GetModelLoader();

            auto& model = registry->get<Components::Model>(entity);
            modelLoader->UnloadModelForEntity(entity, model);

            registry->remove<Components::AnimationData>(entity);
        }

        networkState.networkIDToEntity.erase(networkID);
        networkState.entityToNetworkID.erase(entity);

        registry->destroy(entity);

        return true;
    }
    bool HandleOnEntityDisplayInfoUpdate(Network::SocketID socketID, Network::Message& message)
    {
        GameDefine::ObjectGuid networkID;
        u32 displayID = 0;
        GameDefine::UnitRace race;
        GameDefine::UnitGender gender;

        if (!message.buffer->Deserialize(networkID))
            return false;

        if (!message.buffer->GetU32(displayID))
            return false;

        if (!message.buffer->Get(race))
            return false;

        if (!message.buffer->Get(gender))
            return false;

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& networkState = registry->ctx().get<Singletons::NetworkState>();

        if (!networkState.networkIDToEntity.contains(networkID))
        {
            NC_LOG_WARNING("Network : Received Display Info Update for non existing entity ({0})", networkID.ToString());
            return true;
        }

        entt::entity entity = networkState.networkIDToEntity[networkID];
        if (!registry->valid(entity))
        {
            NC_LOG_WARNING("Network : Received Display Info Update for non existing entity ({0})", networkID.ToString());
            return true;
        }

        ModelLoader* modelLoader = ServiceLocator::GetGameRenderer()->GetModelLoader();
        auto& model = registry->get<ECS::Components::Model>(entity);

        if (!modelLoader->LoadDisplayIDForEntity(entity, model, Database::Unit::DisplayInfoType::Creature, displayID))
        {
            NC_LOG_WARNING("Network : Failed to load DisplayID for entity ({0})", networkID.ToString());
            return true;
        }

        auto& displayInfo = registry->get<Components::DisplayInfo>(entity);
        displayInfo.displayID = displayID;
        displayInfo.race = race;
        displayInfo.gender = gender;

        return true;
    }
    bool HandleOnEntityMove(Network::SocketID socketID, Network::Message& message)
    {
        GameDefine::ObjectGuid networkID;
        vec3 position = vec3(0.0f);
        quat rotation = quat(1.0f, 0.0f, 0.0f, 0.0f);
        Components::MovementFlags movementFlags = {};
        f32 verticalVelocity = 0.0f;

        if (!message.buffer->Deserialize(networkID))
            return false;

        if (!message.buffer->Get(position))
            return false;

        if (!message.buffer->Get(rotation))
            return false;

        if (!message.buffer->Get(movementFlags))
            return false;

        if (!message.buffer->Get(verticalVelocity))
            return false;

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& networkState = registry->ctx().get<Singletons::NetworkState>();

        if (!networkState.networkIDToEntity.contains(networkID))
        {
            NC_LOG_WARNING("Network : Received Entity Move for non existing entity ({0})", networkID.ToString());
            return true;
        }

        entt::entity entity = networkState.networkIDToEntity[networkID];

        auto& unitMovementOverTime = registry->get<Components::UnitMovementOverTime>(entity);
        auto& transform = registry->get<Components::Transform>(entity);
        auto& movementInfo = registry->get<Components::MovementInfo>(entity);

        unitMovementOverTime.startPos = transform.GetWorldPosition();
        unitMovementOverTime.endPos = position;
        unitMovementOverTime.time = 0.0f;
        movementInfo.movementFlags = movementFlags;

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

        movementInfo.verticalVelocity = verticalVelocity;

        TransformSystem& transformSystem = TransformSystem::Get(*registry);
        transformSystem.SetWorldRotation(entity, rotation);

        return true;
    }
    bool HandleOnEntityMoveStop(Network::SocketID socketID, Network::Message& message)
    {
        return true;
    }
    bool HandleOnResourceUpdate(Network::SocketID socketID, Network::Message& message)
    {
        GameDefine::ObjectGuid networkID;
        Components::PowerType powerType = Components::PowerType::Count;
        f32 powerBaseValue = 0.0f;
        f32 powerCurrentValue = 0.0f;
        f32 powerMaxValue = 0.0f;

        if (!message.buffer->Deserialize(networkID))
            return false;

        if (!message.buffer->Get(powerType))
            return false;

        if (!message.buffer->GetF32(powerBaseValue))
            return false;

        if (!message.buffer->GetF32(powerCurrentValue))
            return false;

        if (!message.buffer->GetF32(powerMaxValue))
            return false;

        if (powerType >= Components::PowerType::Count)
        {
            NC_LOG_WARNING("Network : Received Power Update for unknown PowerType ({0})", static_cast<u32>(powerType));
            return true;
        }

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& networkState = registry->ctx().get<Singletons::NetworkState>();

        if (!networkState.networkIDToEntity.contains(networkID))
        {
            NC_LOG_WARNING("Network : Received Power Update for non existing entity ({0})", networkID.ToString());
            return true;
        }

        entt::entity entity = networkState.networkIDToEntity[networkID];
        if (!registry->valid(entity))
        {
            NC_LOG_WARNING("Network : Received Power Update for non existing entity ({0})", networkID.ToString());
            return true;
        }

        auto& unitStatsComponent = registry->get<Components::UnitStatsComponent>(entity);

        if (powerType == Components::PowerType::Health)
        {
            unitStatsComponent.baseHealth = powerBaseValue;
            unitStatsComponent.currentHealth = powerCurrentValue;
            unitStatsComponent.maxHealth = powerMaxValue;
        }
        else
        {
            unitStatsComponent.basePower[(u32)powerType] = powerBaseValue;
            unitStatsComponent.currentPower[(u32)powerType] = powerCurrentValue;
            unitStatsComponent.maxPower[(u32)powerType] = powerMaxValue;
        }

        return true;
    }
    bool HandleOnEntityTargetUpdate(Network::SocketID socketID, Network::Message& message)
    {
        GameDefine::ObjectGuid networkID;
        GameDefine::ObjectGuid targetNetworkID;

        if (!message.buffer->Deserialize(networkID))
            return false;

        if (!message.buffer->Deserialize(targetNetworkID))
            return false;

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& networkState = registry->ctx().get<Singletons::NetworkState>();

        if (!networkState.networkIDToEntity.contains(networkID))
        {
            NC_LOG_WARNING("Network : Received Target Update for non existing entity ({0})", networkID.ToString());
            return true;
        }

        if (!networkState.networkIDToEntity.contains(targetNetworkID))
        {
            NC_LOG_WARNING("Network : Received Target Update for non existing target entity ({0})", targetNetworkID.ToString());
            return true;
        }

        Singletons::CharacterSingleton& characterSingleton = registry->ctx().get<Singletons::CharacterSingleton>();

        entt::entity entity = networkState.networkIDToEntity[networkID];
        entt::entity targetEntity = networkState.networkIDToEntity[targetNetworkID];

        auto& unit = registry->get<Components::Unit>(entity);
        unit.targetEntity = targetEntity;

        return true;
    }
    bool HandleOnEntityCastSpell(Network::SocketID socketID, Network::Message& message)
    {
        GameDefine::ObjectGuid networkID;
        f32 castTime = 0.0f;
        f32 duration = 0.0f;

        if (!message.buffer->Deserialize(networkID))
            return false;

        if (!message.buffer->GetF32(castTime))
            return false;

        if (!message.buffer->GetF32(duration))
            return false;

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& networkState = registry->ctx().get<Singletons::NetworkState>();

        if (!networkState.networkIDToEntity.contains(networkID))
        {
            NC_LOG_WARNING("Network : Received Cast Spell for non existing entity ({0})", networkID.ToString());
            return true;
        }

        entt::entity entity = networkState.networkIDToEntity[networkID];

        if (!registry->valid(entity))
        {
            NC_LOG_WARNING("Network : Received Cast Spell for non existing entity ({0})", networkID.ToString());
            return true;
        }

        Components::Unit* unit = registry->try_get<Components::Unit>(entity);
        if (!unit)
        {
            NC_LOG_WARNING("Network : Received Cast Spell for non existing entity ({0})", networkID.ToString());
            return true;
        }

        Components::CastInfo& castInfo = registry->emplace_or_replace<Components::CastInfo>(entity);

        castInfo.target = unit->targetEntity;
        castInfo.castTime = castTime;
        castInfo.duration = duration;

        return true;
    }
    
    bool HandleOnItemCreate(Network::SocketID socketID, Network::Message& message)
    {
        GameDefine::ObjectGuid networkID;
        u32 itemID = 0;
        u16 count = 0;
        u16 durability = 0;

        bool didFail = false;

        didFail |= !message.buffer->Deserialize(networkID);
        didFail |= !message.buffer->GetU32(itemID);
        didFail |= !message.buffer->GetU16(count);
        didFail |= !message.buffer->GetU16(durability);

        if (didFail)
            return false;

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& networkState = registry->ctx().get<Singletons::NetworkState>();

        if (networkState.networkIDToEntity.contains(networkID))
        {
            NC_LOG_WARNING("Network : Received Create Item for already existing item ({0})", networkID.ToString());
            return true;
        }

        entt::entity newItemEntity = registry->create();

        auto& item = registry->emplace<Components::Item>(newItemEntity);
        item.guid = networkID;
        item.itemID = itemID;
        item.count = count;
        item.durability = durability;

        auto& name = registry->emplace<Components::Name>(newItemEntity);

        std::string itemName = networkID.ToString();
        name.name = itemName;
        name.fullName = itemName;
        name.nameHash = StringUtils::fnv1a_32(itemName.c_str(), itemName.size());

        networkState.networkIDToEntity[networkID] = newItemEntity;
        networkState.entityToNetworkID[newItemEntity] = networkID;

        return true;
    }
    bool HandleOnContainerCreate(Network::SocketID socketID, Network::Message& message)
    {
        u16 containerIndex = 0;
        u32 itemID = 0;
        GameDefine::ObjectGuid networkID;
        u16 numSlots = 0;
        u16 numSlotsFree = 0;

        bool didFail = false;

        didFail |= !message.buffer->GetU16(containerIndex);
        didFail |= !message.buffer->GetU32(itemID);
        didFail |= !message.buffer->Deserialize(networkID);
        didFail |= !message.buffer->GetU16(numSlots);
        didFail |= !message.buffer->GetU16(numSlotsFree);

        if (didFail)
            return false;

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& networkState = registry->ctx().get<Singletons::NetworkState>();

        if (networkState.networkIDToEntity.contains(networkID))
        {
            NC_LOG_WARNING("Network : Received Create Container for already existing container ({0})", networkID.ToString());
            return true;
        }

        Components::Container container =
        {
            .itemID = itemID,
            .numSlots = numSlots,
            .numFreeSlots = numSlotsFree,
        };
        container.items.resize(numSlots);

        u8 numItemsInContainer = numSlots - numSlotsFree;
        for (u32 i = 0; i < numItemsInContainer; i++)
        {
            u16 containerItemIndex = 0;
            GameDefine::ObjectGuid containerItemNetworkID;

            didFail |= !message.buffer->GetU16(containerItemIndex);
            didFail |= !message.buffer->Deserialize(containerItemNetworkID);

            container.items[containerItemIndex] = containerItemNetworkID;
        }

        if (didFail)
            return false;

        entt::entity newContainerEntity = registry->create();

        auto& characterSingleton = registry->ctx().get<Singletons::CharacterSingleton>();

        if (containerIndex == 0)
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
            std::string containerName = networkID.ToString();
            name.name = containerName;
            name.fullName = containerName;
            name.nameHash = StringUtils::fnv1a_32(containerName.c_str(), containerName.size());

            auto& itemComp = registry->emplace<Components::Item>(newContainerEntity);
            itemComp.guid = networkID;
            itemComp.itemID = itemID;
            itemComp.count = 1;
            itemComp.durability = numSlots;

            networkState.networkIDToEntity[networkID] = newContainerEntity;
            networkState.entityToNetworkID[newContainerEntity] = networkID;
        }

        registry->emplace<Components::Container>(newContainerEntity, container);
        characterSingleton.containers[containerIndex] = networkID;

        auto* luaManager = ServiceLocator::GetLuaManager();
        auto playerEventHandler = luaManager->GetLuaHandler<Scripting::PlayerEventHandler*>(Scripting::LuaHandlerType::PlayerEvent);
        
        Scripting::LuaPlayerEventContainerCreateData eventData;
        eventData.index = containerIndex + 1;
        eventData.numSlots = container.numSlots;
        eventData.itemID = container.itemID;

        if (numItemsInContainer > 0)
        {
            eventData.items.reserve(numItemsInContainer);

            entt::registry* dbRegistry = ServiceLocator::GetEnttRegistries()->dbRegistry;
            auto& clientDBSingleton = dbRegistry->ctx().get<Singletons::ClientDBSingleton>();
            auto* itemStorage = clientDBSingleton.Get(ClientDBHash::Item);

            for (u32 i = 0; i < container.numSlots; i++)
            {
                const auto& objectGuid = container.items[i];
                if (!objectGuid.IsValid())
                    continue;

                if (!networkState.networkIDToEntity.contains(objectGuid))
                    continue;

                entt::entity itemEntity = networkState.networkIDToEntity[objectGuid];
                const auto& item = registry->get<Components::Item>(itemEntity);

                if (!itemStorage->Has(item.itemID))
                    continue;

                const auto& itemInfo = itemStorage->Get<Generated::ItemRecord>(item.itemID);

                auto& eventItemData = eventData.items.emplace_back();
                eventItemData.slot = i + 1;
                eventItemData.itemID = item.itemID;
                eventItemData.count = item.count;
            }
        }

        playerEventHandler->CallEvent(luaManager->GetInternalState(), static_cast<u32>(Scripting::LuaPlayerEvent::ContainerCreate), &eventData);
        return true;
    }
    bool HandleOnContainerAddToSlot(Network::SocketID socketID, Network::Message& message)
    {
        u16 containerIndex = 0;
        u16 slotIndex = 0;
        GameDefine::ObjectGuid itemNetworkID;

        bool didFail = false;

        didFail |= !message.buffer->GetU16(containerIndex);
        didFail |= !message.buffer->GetU16(slotIndex);
        didFail |= !message.buffer->Deserialize(itemNetworkID);

        if (didFail)
            return false;

        if (containerIndex >= 6)
        {
            NC_LOG_WARNING("Network : Received Container Add To Slot for invalid container index ({0})", containerIndex);
            return true;
        }

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& characterSingleton = registry->ctx().get<Singletons::CharacterSingleton>();
        auto& networkState = registry->ctx().get<Singletons::NetworkState>();

        if (!networkState.networkIDToEntity.contains(itemNetworkID))
        {
            NC_LOG_WARNING("Network : Received Container Add To Slot for non existing item ({0})", itemNetworkID.ToString());
            return true;
        }

        Components::Container* containerPtr = nullptr;
        if (containerIndex == 0)
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
            if (!characterSingleton.containers[containerIndex].IsValid())
            {
                NC_LOG_WARNING("Network : Received Container Add To Slot for non existing container ({0})", containerIndex);
                return true;
            }

            GameDefine::ObjectGuid containerNetworkID = characterSingleton.containers[containerIndex];
            if (!networkState.networkIDToEntity.contains(containerNetworkID))
            {
                NC_LOG_WARNING("Network : Received Container Add To Slot for non existing container entity ({0})", containerNetworkID.ToString());
                return true;
            }

            entt::entity containerEntity = networkState.networkIDToEntity[containerNetworkID];
            if (!registry->valid(containerEntity))
            {
                NC_LOG_WARNING("Network : Received Container Add To Slot for invalid container entity ({0})", containerNetworkID.ToString());
                return true;
            }

            auto& container = registry->get<Components::Container>(containerEntity);
            containerPtr = &container;
        }

        containerPtr->AddToSlot(slotIndex, itemNetworkID);

        entt::entity itemEntity = networkState.networkIDToEntity[itemNetworkID];
        auto& item = registry->get<Components::Item>(itemEntity);

        Scripting::LuaPlayerEventContainerAddToSlotData eventData =
        {
            .containerIndex = (u32)containerIndex + 1,
            .slotIndex = (u32)slotIndex + 1u,
            .itemID = item.itemID,
            .count = item.count
        };

        auto* luaManager = ServiceLocator::GetLuaManager();
        auto playerEventHandler = luaManager->GetLuaHandler<Scripting::PlayerEventHandler*>(Scripting::LuaHandlerType::PlayerEvent);
        playerEventHandler->CallEvent(luaManager->GetInternalState(), static_cast<u32>(Scripting::LuaPlayerEvent::ContainerAddToSlot), &eventData);
        return true;
    }
    bool HandleOnContainerRemoveFromSlot(Network::SocketID socketID, Network::Message& message)
    {
        u16 containerIndex = 0;
        u16 slotIndex = 0;

        bool didFail = false;
        didFail |= !message.buffer->GetU16(containerIndex);
        didFail |= !message.buffer->GetU16(slotIndex);

        if (didFail)
            return false;

        if (containerIndex >= 6)
        {
            NC_LOG_WARNING("Network : Received Container Remove From Slot for invalid container index ({0})", containerIndex);
            return true;
        }

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& characterSingleton = registry->ctx().get<Singletons::CharacterSingleton>();
        auto& networkState = registry->ctx().get<Singletons::NetworkState>();

        Components::Container* containerPtr = nullptr;
        if (containerIndex == 0)
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
            if (!characterSingleton.containers[containerIndex].IsValid())
            {
                NC_LOG_WARNING("Network : Received Container Remove From Slot for non existing container ({0})", containerIndex);
                return true;
            }

            GameDefine::ObjectGuid containerNetworkID = characterSingleton.containers[containerIndex];
            if (!networkState.networkIDToEntity.contains(containerNetworkID))
            {
                NC_LOG_WARNING("Network : Received Container Remove From Slot for non existing container entity ({0})", containerNetworkID.ToString());
                return true;
            }

            entt::entity containerEntity = networkState.networkIDToEntity[containerNetworkID];
            if (!registry->valid(containerEntity))
            {
                NC_LOG_WARNING("Network : Received Container Remove From Slot for invalid container entity ({0})", containerNetworkID.ToString());
                return true;
            }

            auto& container = registry->get<Components::Container>(containerEntity);
            containerPtr = &container;
        }

        GameDefine::ObjectGuid itemNetworkID = containerPtr->GetItem(slotIndex);
        containerPtr->RemoveFromSlot(slotIndex);

        entt::entity itemEntity = networkState.networkIDToEntity[itemNetworkID];
        networkState.networkIDToEntity.erase(itemNetworkID);
        registry->destroy(itemEntity);

        Scripting::LuaPlayerEventContainerRemoveFromSlotData eventData =
        {
            .containerIndex = (u32)containerIndex + 1,
            .slotIndex = (u32)slotIndex + 1u
        };

        auto* luaManager = ServiceLocator::GetLuaManager();
        auto playerEventHandler = luaManager->GetLuaHandler<Scripting::PlayerEventHandler*>(Scripting::LuaHandlerType::PlayerEvent);
        playerEventHandler->CallEvent(luaManager->GetInternalState(), static_cast<u32>(Scripting::LuaPlayerEvent::ContainerRemoveFromSlot), &eventData);
        return true;
    }
    bool HandleOnContainerSwapSlots(Network::SocketID socketID, Network::Message& message)
    {
        u16 srcContainerIndex = 0;
        u16 destContainerIndex = 0;
        u16 srcSlotIndex = 0;
        u16 dstSlotIndex = 0;

        bool didFail = false;

        didFail |= !message.buffer->GetU16(srcContainerIndex);
        didFail |= !message.buffer->GetU16(destContainerIndex);
        didFail |= !message.buffer->GetU16(srcSlotIndex);
        didFail |= !message.buffer->GetU16(dstSlotIndex);

        if (didFail)
            return false;

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& characterSingleton = registry->ctx().get<Singletons::CharacterSingleton>();
        auto& networkState = registry->ctx().get<Singletons::NetworkState>();
        
        Components::Container* srcContainer = nullptr;
        Components::Container* destContainer = nullptr;

        if (srcContainerIndex == 0)
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
            if (!characterSingleton.containers[srcContainerIndex].IsValid())
            {
                NC_LOG_WARNING("Network : Received Container Swap Slots for non existing container ({0})", srcContainerIndex);
                return true;
            }

            GameDefine::ObjectGuid containerNetworkID = characterSingleton.containers[srcContainerIndex];
            if (!networkState.networkIDToEntity.contains(containerNetworkID))
            {
                NC_LOG_WARNING("Network : Received Container Swap Slots for non existing container entity ({0})", containerNetworkID.ToString());
                return true;
            }

            entt::entity containerEntity = networkState.networkIDToEntity[containerNetworkID];
            if (!registry->valid(containerEntity))
            {
                NC_LOG_WARNING("Network : Received Container Swap Slots for invalid container entity ({0})", containerNetworkID.ToString());
                return true;
            }

            auto& container = registry->get<Components::Container>(containerEntity);
            srcContainer = &container;
        }

        if (srcContainerIndex == destContainerIndex)
        {
            destContainer = srcContainer;
        }
        else
        {
            if (destContainerIndex == 0)
            {
                if (!registry->valid(characterSingleton.baseContainerEntity))
                {
                    NC_LOG_WARNING("Network : Received Container Swap Slots for non existing base container");
                    return true;
                }

                auto& baseContainer = registry->get<Components::Container>(characterSingleton.baseContainerEntity);
                destContainer = &baseContainer;
            }
            else
            {
                if (!characterSingleton.containers[destContainerIndex].IsValid())
                {
                    NC_LOG_WARNING("Network : Received Container Swap Slots for non existing container ({0})", destContainerIndex);
                    return true;
                }

                GameDefine::ObjectGuid containerNetworkID = characterSingleton.containers[destContainerIndex];
                if (!networkState.networkIDToEntity.contains(containerNetworkID))
                {
                    NC_LOG_WARNING("Network : Received Container Swap Slots for non existing container entity ({0})", containerNetworkID.ToString());
                    return true;
                }

                entt::entity containerEntity = networkState.networkIDToEntity[containerNetworkID];
                if (!registry->valid(containerEntity))
                {
                    NC_LOG_WARNING("Network : Received Container Swap Slots for invalid container entity ({0})", containerNetworkID.ToString());
                    return true;
                }

                auto& container = registry->get<Components::Container>(containerEntity);
                destContainer = &container;
            }
        }

        std::swap(srcContainer->items[srcSlotIndex], destContainer->items[dstSlotIndex]);

        Scripting::LuaPlayerEventContainerSwapSlotsData eventData =
        {
            .srcContainerIndex = (u32)srcContainerIndex + 1,
            .destContainerIndex = (u32)destContainerIndex + 1,
            .srcSlotIndex = (u32)srcSlotIndex + 1u,
            .destSlotIndex = (u32)dstSlotIndex + 1u,
        };

        auto* luaManager = ServiceLocator::GetLuaManager();
        auto playerEventHandler = luaManager->GetLuaHandler<Scripting::PlayerEventHandler*>(Scripting::LuaHandlerType::PlayerEvent);
        playerEventHandler->CallEvent(luaManager->GetInternalState(), static_cast<u32>(Scripting::LuaPlayerEvent::ContainerSwapSlots), &eventData);
        return true;
    }
    bool HandleOnSpellCastResult(Network::SocketID socketID, Network::Message& message)
    {
        u8 result;
        if (!message.buffer->GetU8(result))
            return false;

        if (result != 0)
        {
            std::string errorText = "";

            if (!message.buffer->GetString(errorText))
                return false;

            NC_LOG_WARNING("Network : Spell Cast Failed - {0}", errorText);
        }
        else
        {
            f32 castTime = 0.0f;
            f32 duration = 0.0f;

            if (!message.buffer->GetF32(castTime))
                return false;

            if (!message.buffer->GetF32(duration))
                return false;

            entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
            Singletons::CharacterSingleton& characterSingleton = registry->ctx().get<Singletons::CharacterSingleton>();

            auto& unit = registry->get<Components::Unit>(characterSingleton.moverEntity);
            auto& castInfo = registry->emplace_or_replace<Components::CastInfo>(characterSingleton.moverEntity);
            castInfo.target = unit.targetEntity;
            castInfo.castTime = castTime;
            castInfo.duration = duration;
        }

        return true;
    }
    bool HandleOnCombatEvent(Network::SocketID socketID, Network::Message& message)
    {
        u16 eventID = 0;
        GameDefine::ObjectGuid sourceNetworkID;

        if (!message.buffer->GetU16(eventID))
            return false;

        if (!message.buffer->Deserialize(sourceNetworkID))
            return false;

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& networkState = registry->ctx().get<Singletons::NetworkState>();

        if (!networkState.networkIDToEntity.contains(sourceNetworkID))
        {
            NC_LOG_WARNING("Network : Received Combat Event for non existing entity ({0})", sourceNetworkID.ToString());
            return true;
        }

        entt::entity sourceEntity = networkState.networkIDToEntity[sourceNetworkID];

        if (!registry->valid(sourceEntity))
        {
            NC_LOG_WARNING("Network : Received Combat Event for non existing entity ({0})", sourceNetworkID.ToString());
            return true;
        }

        switch (eventID)
        {
            // Damage Taken
            case 0:
            case 1:
            {
                GameDefine::ObjectGuid targetNetworkID;
                f32 value = 0.0f;

                if (!message.buffer->Deserialize(targetNetworkID))
                    return false;

                if (!message.buffer->GetF32(value))
                    return false;

                if (!networkState.networkIDToEntity.contains(targetNetworkID))
                {
                    NC_LOG_WARNING("Network : Received Combat Event for non existing target entity ({0})", targetNetworkID.ToString());
                    return true;
                }

                entt::entity targetEntity = networkState.networkIDToEntity[targetNetworkID];

                if (!registry->valid(targetEntity))
                {
                    NC_LOG_WARNING("Network : Received Combat Event for non existing target entity ({0})", targetNetworkID.ToString());
                    return true;
                }

                Components::Name* sourceName = registry->try_get<Components::Name>(sourceEntity);
                Components::Name* targetName = registry->try_get<Components::Name>(targetEntity);

                if (!sourceName || !targetName)
                {
                    NC_LOG_WARNING("Network : Received Combat Event for entity without Name Component ({0})", targetNetworkID.ToString());
                    return true;
                }

                if (eventID == 0)
                {
                    // Damage Dealt
                    ImGui::InsertNotification({ ImGuiToastType::Success, 3000, "%s dealt %.2f damage to %s", sourceName->name.c_str(), value, targetName->name.c_str() });
                }
                else
                {
                    // Healing Taken
                    ImGui::InsertNotification({ ImGuiToastType::Success, 3000, "%s healed %s for %.2f", sourceName->name.c_str(), value, targetName->name.c_str() });
                }

                break;
            }

            case 2:
            {
                GameDefine::ObjectGuid targetNetworkID;
                if (!message.buffer->Deserialize(targetNetworkID))
                    return false;

                if (!networkState.networkIDToEntity.contains(targetNetworkID))
                {
                    NC_LOG_WARNING("Network : Received Combat Event for non existing target entity ({0})", targetNetworkID.ToString());
                    return true;
                }

                entt::entity targetEntity = networkState.networkIDToEntity[targetNetworkID];

                if (!registry->valid(targetEntity))
                {
                    NC_LOG_WARNING("Network : Received Combat Event for non existing target entity ({0})", targetNetworkID.ToString());
                    return true;
                }

                Components::Name* sourceName = registry->try_get<Components::Name>(sourceEntity);
                Components::Name* targetName = registry->try_get<Components::Name>(targetEntity);

                if (!sourceName || !targetName)
                {
                    NC_LOG_WARNING("Network : Received Combat Event for entity without Name Component ({0})", targetNetworkID.ToString());
                    return true;
                }

                ImGui::InsertNotification({ ImGuiToastType::Success, 3000, "%s ressurected %s", sourceName->name.c_str(), targetName->name.c_str() });
                break;
            }

            default:
            {
                break;
            }
        }
        return true;
    }
    bool HandleOnTriggerCreate(Network::SocketID socketID, Network::Message& message)
    {
        u32 triggerID;
        std::string name;
        Generated::ProximityTriggerFlagEnum flags;
        u16 mapID;
        vec3 position;
        vec3 extents;

        if (!message.buffer->GetU32(triggerID))
            return false;

        if (!message.buffer->GetString(name))
            return false;

        if (!message.buffer->Get(flags))
            return false;

        if (!message.buffer->GetU16(mapID))
            return false;

        if (!message.buffer->Get(position))
            return false;

        if (!message.buffer->Get(extents))
            return false;

        entt::registry& registry = *ServiceLocator::GetEnttRegistries()->gameRegistry;
        ECS::Util::ProximityTriggerUtil::CreateTrigger(registry, triggerID, name, flags, mapID, position, extents);
        return true;
    }
    bool HandleOnTriggerDestroy(Network::SocketID socketID, Network::Message& message)
    {
        u32 triggerID;

        if (!message.buffer->GetU32(triggerID))
            return false;

        entt::registry& registry = *ServiceLocator::GetEnttRegistries()->gameRegistry;
        ECS::Util::ProximityTriggerUtil::DestroyTrigger(registry, triggerID);
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

            networkState.gameMessageRouter = std::make_unique<Network::GameMessageRouter>();
            networkState.gameMessageRouter->SetMessageHandler(Network::GameOpcode::Server_Connected,                Network::GameMessageHandler(Network::ConnectionStatus::None,        0u, -1, &HandleOnConnected));
            networkState.gameMessageRouter->SetMessageHandler(Network::GameOpcode::Shared_Ping,                     Network::GameMessageHandler(Network::ConnectionStatus::Connected,   0u, -1, &HandleOnPong));
            networkState.gameMessageRouter->SetMessageHandler(Network::GameOpcode::Server_UpdateStats,              Network::GameMessageHandler(Network::ConnectionStatus::Connected,   0u, -1, &HandleOnUpdateStats));
            
            networkState.gameMessageRouter->SetMessageHandler(Network::GameOpcode::Server_SendCheatCommandResult,   Network::GameMessageHandler(Network::ConnectionStatus::Connected,   0u, -1, &HandleOnCheatCommandResult));

            networkState.gameMessageRouter->SetMessageHandler(Network::GameOpcode::Server_SetMover,                 Network::GameMessageHandler(Network::ConnectionStatus::Connected,   0u, -1, &HandleOnSetMover));
            networkState.gameMessageRouter->SetMessageHandler(Network::GameOpcode::Server_EntityCreate,             Network::GameMessageHandler(Network::ConnectionStatus::Connected,   0u, -1, &HandleOnEntityCreate));
            networkState.gameMessageRouter->SetMessageHandler(Network::GameOpcode::Server_EntityDestroy,            Network::GameMessageHandler(Network::ConnectionStatus::Connected,   0u, -1, &HandleOnEntityDestroy));
            networkState.gameMessageRouter->SetMessageHandler(Network::GameOpcode::Server_EntityDisplayInfoUpdate,  Network::GameMessageHandler(Network::ConnectionStatus::Connected,   0u, -1, &HandleOnEntityDisplayInfoUpdate));
            networkState.gameMessageRouter->SetMessageHandler(Network::GameOpcode::Shared_EntityMove,               Network::GameMessageHandler(Network::ConnectionStatus::Connected,   0u, -1, &HandleOnEntityMove));
            networkState.gameMessageRouter->SetMessageHandler(Network::GameOpcode::Shared_EntityMoveStop,           Network::GameMessageHandler(Network::ConnectionStatus::Connected,   0u, -1, &HandleOnEntityMoveStop));
            networkState.gameMessageRouter->SetMessageHandler(Network::GameOpcode::Server_EntityResourcesUpdate,    Network::GameMessageHandler(Network::ConnectionStatus::Connected,   0u, -1, &HandleOnResourceUpdate));
            networkState.gameMessageRouter->SetMessageHandler(Network::GameOpcode::Shared_EntityTargetUpdate,       Network::GameMessageHandler(Network::ConnectionStatus::Connected,   0u, -1, &HandleOnEntityTargetUpdate));
            networkState.gameMessageRouter->SetMessageHandler(Network::GameOpcode::Server_EntityCastSpell,          Network::GameMessageHandler(Network::ConnectionStatus::Connected,   0u, -1, &HandleOnEntityCastSpell));

            networkState.gameMessageRouter->SetMessageHandler(Network::GameOpcode::Server_ItemCreate,               Network::GameMessageHandler(Network::ConnectionStatus::Connected,   0u, -1, &HandleOnItemCreate));
            networkState.gameMessageRouter->SetMessageHandler(Network::GameOpcode::Server_ContainerCreate,          Network::GameMessageHandler(Network::ConnectionStatus::Connected,   0u, -1, &HandleOnContainerCreate));
            networkState.gameMessageRouter->SetMessageHandler(Network::GameOpcode::Server_ContainerAddToSlot,       Network::GameMessageHandler(Network::ConnectionStatus::Connected,   0u, -1, &HandleOnContainerAddToSlot));
            networkState.gameMessageRouter->SetMessageHandler(Network::GameOpcode::Server_ContainerRemoveFromSlot,  Network::GameMessageHandler(Network::ConnectionStatus::Connected,   0u, -1, &HandleOnContainerRemoveFromSlot));
            networkState.gameMessageRouter->SetMessageHandler(Network::GameOpcode::Server_ContainerSwapSlots,       Network::GameMessageHandler(Network::ConnectionStatus::Connected,   0u, -1, &HandleOnContainerSwapSlots));

            networkState.gameMessageRouter->SetMessageHandler(Network::GameOpcode::Server_SendSpellCastResult,      Network::GameMessageHandler(Network::ConnectionStatus::Connected,   0u, -1, &HandleOnSpellCastResult));
            networkState.gameMessageRouter->SetMessageHandler(Network::GameOpcode::Server_SendCombatEvent,          Network::GameMessageHandler(Network::ConnectionStatus::Connected,   0u, -1, &HandleOnCombatEvent));

            networkState.gameMessageRouter->SetMessageHandler(Network::GameOpcode::Server_TriggerCreate,            Network::GameMessageHandler(Network::ConnectionStatus::Connected,   0u, -1, &HandleOnTriggerCreate));
            networkState.gameMessageRouter->SetMessageHandler(Network::GameOpcode::Server_TriggerDestroy,           Network::GameMessageHandler(Network::ConnectionStatus::Connected,   0u, -1, &HandleOnTriggerDestroy));
        }
    }

    void NetworkConnection::Update(entt::registry& registry, f32 deltaTime)
    {
        ZoneScopedN("ECS::NetworkConnection");

        entt::registry::context& ctx = registry.ctx();
        auto& networkState = ctx.get<Singletons::NetworkState>();

        // Restart AsioThread If Needed
        {
            if (networkState.client && networkState.client->IsConnected())
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
        if (networkState.client->IsConnected())
        {
            auto currentTime = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

            if (!wasConnected)
            {
                // Just connected
                wasConnected = true;

                networkState.lastPingTime = currentTime;
                CharacterController::DeleteCharacterController(registry, true);
            }
            else
            {
                u64 timeDiff = currentTime - networkState.lastPingTime;
                if (timeDiff >= Singletons::NetworkState::PING_INTERVAL)
                {
                    std::shared_ptr<Bytebuffer> buffer = Bytebuffer::Borrow<16>();
                    if (Util::MessageBuilder::Heartbeat::BuildPingMessage(buffer, networkState.ping))
                    {
                        networkState.client->Send(buffer);
                        networkState.lastPingTime = currentTime;
                    }
                }

                if (networkState.lastPongTime != 0u)
                {
                    u64 timeDiff = currentTime - networkState.lastPongTime;
                    if (currentTime - networkState.lastPongTime > Singletons::NetworkState::PING_INTERVAL)
                    {
                        networkState.ping = static_cast<u16>(timeDiff);
                    }
                }
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

                networkState.lastPingTime = 0u;
                networkState.lastPongTime = 0u;
                networkState.ping = 0;
                networkState.serverUpdateDiff = 0;
                networkState.pingHistoryIndex = 0;
                networkState.pingHistory.fill(0);
                networkState.pingHistorySize = 0;
                networkState.entityToNetworkID.clear();
                networkState.networkIDToEntity.clear();
                networkState.asioContext.stop();

                if (networkState.asioThread.joinable())
                    networkState.asioThread.join();

                ServiceLocator::GetLuaManager()->SetDirty();
                CharacterController::InitCharacterController(registry, true);
            }

            return;
        }

        // Handle 'SocketMessageEvent'
        {
            moodycamel::ConcurrentQueue<Network::SocketMessageEvent>& messageEvents = networkState.client->GetMessageEvents();

            Network::SocketMessageEvent messageEvent;
            while (messageEvents.try_dequeue(messageEvent))
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