#include "NetworkConnection.h"
#include "CharacterController.h"

#include "Game/Animation/AnimationSystem.h"
#include "Game/ECS/Components/AABB.h"
#include "Game/ECS/Components/CastInfo.h"
#include "Game/ECS/Components/DisplayInfo.h"
#include "Game/ECS/Components/Model.h"
#include "Game/ECS/Components/MovementInfo.h"
#include "Game/ECS/Components/Name.h"
#include "Game/ECS/Components/NetworkedEntity.h"
#include "Game/ECS/Components/UnitStatsComponent.h"
#include "Game/ECS/Singletons/CharacterSingleton.h"
#include "Game/ECS/Singletons/NetworkState.h"
#include "Game/ECS/Util/Transforms.h"
#include "Game/Rendering/GameRenderer.h"
#include "Game/Rendering/Model/ModelLoader.h"
#include "Game/Util/ServiceLocator.h"

#include <Base/CVarSystem/CVarSystem.h>
#include <Base/Util/DebugHandler.h>

#include <Gameplay/Network/GameMessageRouter.h>

#include <Network/Client.h>
#include <Network/Define.h>

#include <entt/entt.hpp>
#include <imgui/ImGuiNotify.hpp>

namespace ECS::Systems
{
    bool HandleOnConnected(Network::SocketID socketID, std::shared_ptr<Bytebuffer>& recvPacket)
    {
        Network::ConnectResult result = Network::ConnectResult::None;
        if (!recvPacket->Get(result))
            return false;

        if (result != Network::ConnectResult::Success)
        {
            NC_LOG_WARNING("Network : Failed to login to character");
            return false;
        }

        NC_LOG_INFO("Network : Logged in to character");
        return true;
    }
    
    bool HandleOnCheatCommandResult(Network::SocketID socketID, std::shared_ptr<Bytebuffer>& recvPacket)
    {
        u8 result = 0;
        std::string resultText = "";

        if (!recvPacket->GetU8(result))
            return false;

        if (!recvPacket->GetString(resultText))
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

    bool HandleOnSetMover(Network::SocketID socketID, std::shared_ptr<Bytebuffer>& recvPacket)
    {
        entt::entity networkID = entt::null;
        if (!recvPacket->Get(networkID))
            return false;

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& networkState = registry->ctx().get<Singletons::NetworkState>();

        if (!networkState.networkIDToEntity.contains(networkID))
        {
            NC_LOG_WARNING("Network : Received SetMover for non-existent entity ({0})", entt::to_integral(networkID));
            return true;
        }

        auto& characterSingleton = registry->ctx().get<Singletons::CharacterSingleton>();
        entt::entity entity = networkState.networkIDToEntity[networkID];

        characterSingleton.moverEntity = entity;
        CharacterController::InitCharacterController(*registry, false);

        return true;
    }
    bool HandleOnEntityCreate(Network::SocketID socketID, std::shared_ptr<Bytebuffer>& recvPacket)
    {
        entt::entity networkID = entt::null;
        vec3 position = vec3(0.0f);
        quat rotation = quat(1.0f, 0.0f, 0.0f, 0.0f);
        vec3 scale = vec3(1.0f);

        if (!recvPacket->Get(networkID))
            return false;

        if (!recvPacket->Get(position))
            return false;

        if (!recvPacket->Get(rotation))
            return false;

        if (!recvPacket->Get(scale))
            return false;

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& networkState = registry->ctx().get<Singletons::NetworkState>();

        if (networkState.networkIDToEntity.contains(networkID))
        {
            NC_LOG_WARNING("Network : Received Create Entity for already existing entity ({0})", entt::to_integral(networkID));
            return true;
        }

        entt::entity newEntity = registry->create();
        registry->emplace<Components::AABB>(newEntity);
        registry->emplace<Components::Transform>(newEntity);
        registry->emplace<Components::Name>(newEntity);
        registry->emplace<Components::Model>(newEntity);
        registry->emplace<Components::MovementInfo>(newEntity);
        registry->emplace<Components::UnitStatsComponent>(newEntity);
        auto& displayInfo = registry->emplace<Components::DisplayInfo>(newEntity);
        displayInfo.displayID = 0;

        auto& networkedEntity = registry->emplace<Components::NetworkedEntity>(newEntity);
        networkedEntity.networkID = networkID;
        networkedEntity.targetEntity = entt::null;

        TransformSystem& transformSystem = TransformSystem::Get(*registry);
        transformSystem.SetWorldPosition(newEntity, position);
        transformSystem.SetWorldRotation(newEntity, rotation);
        transformSystem.SetLocalScale(newEntity, scale);

        networkState.networkIDToEntity[networkID] = newEntity;
        networkState.entityToNetworkID[newEntity] = networkID;

        return true;
    }
    bool HandleOnEntityDestroy(Network::SocketID socketID, std::shared_ptr<Bytebuffer>& recvPacket)
    {
        entt::entity networkID = entt::null;

        if (!recvPacket->Get(networkID))
            return false;

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& networkState = registry->ctx().get<Singletons::NetworkState>();

        if (!networkState.networkIDToEntity.contains(networkID))
        {
            NC_LOG_WARNING("Network : Received Delete Entity for unknown entity ({0})", entt::to_integral(networkID));
            return true;
        }

        entt::entity entity = networkState.networkIDToEntity[networkID];

        if (registry->any_of<Components::Model>(entity))
        {
            Animation::AnimationSystem* AnimationSystem = ServiceLocator::GetAnimationSystem();
            ModelLoader* modelLoader = ServiceLocator::GetGameRenderer()->GetModelLoader();

            auto& model = registry->get<Components::Model>(entity);
            AnimationSystem->RemoveInstance(model.instanceID);
            modelLoader->UnloadModelForEntity(entity, model.instanceID);
        }

        networkState.networkIDToEntity.erase(networkID);
        networkState.entityToNetworkID.erase(entity);

        registry->destroy(entity);

        return true;
    }
    bool HandleOnEntityDisplayInfoUpdate(Network::SocketID socketID, std::shared_ptr<Bytebuffer>& recvPacket)
    {
        entt::entity networkID = entt::null;
        u32 displayID = 0;

        if (!recvPacket->Get(networkID))
            return false;

        if (!recvPacket->GetU32(displayID))
            return false;

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& networkState = registry->ctx().get<Singletons::NetworkState>();

        if (!networkState.networkIDToEntity.contains(networkID))
        {
            NC_LOG_WARNING("Network : Received Display Info Update for non existing entity ({0})", entt::to_integral(networkID));
            return true;
        }

        entt::entity entity = networkState.networkIDToEntity[networkID];
        if (!registry->valid(entity))
        {
            NC_LOG_WARNING("Network : Received Display Info Update for non existing entity ({0})", entt::to_integral(networkID));
            return true;
        }

        ModelLoader* modelLoader = ServiceLocator::GetGameRenderer()->GetModelLoader();
        if (!modelLoader->LoadDisplayIDForEntity(entity, displayID))
        {
            NC_LOG_WARNING("Network : Failed to load DisplayID for entity ({0})", entt::to_integral(networkID));
            return true;
        }

        auto& displayInfo = registry->get<Components::DisplayInfo>(entity);
        displayInfo.displayID = displayID;

        return true;
    }
    bool HandleOnEntityMove(Network::SocketID socketID, std::shared_ptr<Bytebuffer>& recvPacket)
    {
        entt::entity networkID = entt::null;
        vec3 position = vec3(0.0f);
        quat rotation = quat(1.0f, 0.0f, 0.0f, 0.0f);
        Components::MovementFlags movementFlags = {};
        f32 verticalVelocity = 0.0f;

        if (!recvPacket->Get(networkID))
            return false;

        if (!recvPacket->Get(position))
            return false;

        if (!recvPacket->Get(rotation))
            return false;

        if (!recvPacket->Get(movementFlags))
            return false;

        if (!recvPacket->Get(verticalVelocity))
            return false;

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& networkState = registry->ctx().get<Singletons::NetworkState>();

        if (!networkState.networkIDToEntity.contains(networkID))
        {
            NC_LOG_WARNING("Network : Received Entity Move for non existing entity ({0})", entt::to_integral(networkID));
            return true;
        }

        entt::entity entity = networkState.networkIDToEntity[networkID];

        auto& networkedEntity = registry->get<Components::NetworkedEntity>(entity);
        auto& transform = registry->get<Components::Transform>(entity);
        auto& movementInfo = registry->get<Components::MovementInfo>(entity);

        networkedEntity.initialPosition = transform.GetWorldPosition();
        networkedEntity.desiredPosition = position;
        networkedEntity.positionProgress = 0.0f;
        movementInfo.movementFlags = movementFlags;
        movementInfo.verticalVelocity = verticalVelocity;

        TransformSystem& transformSystem = TransformSystem::Get(*registry);
        transformSystem.SetWorldRotation(entity, rotation);

        return true;
    }
    bool HandleOnEntityMoveStop(Network::SocketID socketID, std::shared_ptr<Bytebuffer>& recvPacket)
    {
        return true;
    }
    bool HandleOnResourceUpdate(Network::SocketID socketID, std::shared_ptr<Bytebuffer>& recvPacket)
    {
        entt::entity networkID = entt::null;
        Components::PowerType powerType = Components::PowerType::Count;
        f32 powerBaseValue = 0.0f;
        f32 powerCurrentValue = 0.0f;
        f32 powerMaxValue = 0.0f;

        if (!recvPacket->Get(networkID))
            return false;

        if (!recvPacket->Get(powerType))
            return false;

        if (!recvPacket->GetF32(powerBaseValue))
            return false;

        if (!recvPacket->GetF32(powerCurrentValue))
            return false;

        if (!recvPacket->GetF32(powerMaxValue))
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
            NC_LOG_WARNING("Network : Received Power Update for non existing entity ({0})", entt::to_integral(networkID));
            return true;
        }

        entt::entity entity = networkState.networkIDToEntity[networkID];

        if (!registry->valid(entity))
        {
            NC_LOG_WARNING("Network : Received Power Update for non existing entity ({0})", entt::to_integral(networkID));
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
    bool HandleOnEntityTargetUpdate(Network::SocketID socketID, std::shared_ptr<Bytebuffer>& recvPacket)
    {
        entt::entity networkID = entt::null;
        entt::entity targetNetworkID = entt::null;

        if (!recvPacket->Get(networkID))
            return false;

        if (!recvPacket->Get(targetNetworkID))
            return false;

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& networkState = registry->ctx().get<Singletons::NetworkState>();

        if (!networkState.networkIDToEntity.contains(networkID))
        {
            NC_LOG_WARNING("Network : Received Target Update for non existing entity ({0})", entt::to_integral(networkID));
            return true;
        }

        if (!networkState.networkIDToEntity.contains(targetNetworkID))
        {
            NC_LOG_WARNING("Network : Received Target Update for non existing target entity ({0})", entt::to_integral(targetNetworkID));
            return true;
        }

        Singletons::CharacterSingleton& characterSingleton = registry->ctx().get<Singletons::CharacterSingleton>();

        entt::entity entity = networkState.networkIDToEntity[networkID];
        entt::entity targetEntity = networkState.networkIDToEntity[targetNetworkID];

        auto& networkedEntity = registry->get<Components::NetworkedEntity>(entity);
        networkedEntity.targetEntity = targetEntity;

        return true;
    }

    bool HandleOnEntityCastSpell(Network::SocketID socketID, std::shared_ptr<Bytebuffer>& recvPacket)
    {
        entt::entity networkID = entt::null;
        f32 castTime = 0.0f;
        f32 duration = 0.0f;

        if (!recvPacket->Get(networkID))
            return false;

        if (!recvPacket->GetF32(castTime))
            return false;

        if (!recvPacket->GetF32(duration))
            return false;

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& networkState = registry->ctx().get<Singletons::NetworkState>();

        if (!networkState.networkIDToEntity.contains(networkID))
        {
            NC_LOG_WARNING("Network : Received Cast Spell for non existing entity ({0})", entt::to_integral(networkID));
            return true;
        }

        entt::entity entity = networkState.networkIDToEntity[networkID];

        if (!registry->valid(entity))
        {
            NC_LOG_WARNING("Network : Received Cast Spell for non existing entity ({0})", entt::to_integral(networkID));
            return true;
        }

        Components::NetworkedEntity* networkedEntity = registry->try_get<Components::NetworkedEntity>(entity);
        if (!networkedEntity)
        {
            NC_LOG_WARNING("Network : Received Cast Spell for non existing entity ({0})", entt::to_integral(networkID));
            return true;
        }

        Components::CastInfo& castInfo = registry->emplace_or_replace<Components::CastInfo>(entity);

        castInfo.target = networkedEntity->targetEntity;
        castInfo.castTime = castTime;
        castInfo.duration = duration;

        return true;
    }

    bool HandleOnSpellCastResult(Network::SocketID socketID, std::shared_ptr<Bytebuffer>& recvPacket)
    {
        u8 result;

        if (!recvPacket->GetU8(result))
            return false;

        if (result != 0)
        {
            std::string errorText = "";

            if (!recvPacket->GetString(errorText))
                return false;

            NC_LOG_WARNING("Network : Spell Cast Failed - {0}", errorText);
        }
        else
        {
            f32 castTime = 0.0f;
            f32 duration = 0.0f;

            if (!recvPacket->GetF32(castTime))
                return false;

            if (!recvPacket->GetF32(duration))
                return false;

            entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
            Singletons::CharacterSingleton& characterSingleton = registry->ctx().get<Singletons::CharacterSingleton>();

            auto& networkedEntity = registry->get<Components::NetworkedEntity>(characterSingleton.moverEntity);
            auto& castInfo = registry->emplace_or_replace<Components::CastInfo>(characterSingleton.moverEntity);
            castInfo.target = networkedEntity.targetEntity;
            castInfo.castTime = castTime;
            castInfo.duration = duration;
        }

        return true;
    }
    
    bool HandleOnCombatEvent(Network::SocketID socketID, std::shared_ptr<Bytebuffer>& recvPacket)
    {
        u16 eventID = 0;
        entt::entity sourceNetworkID = entt::null;

        if (!recvPacket->GetU16(eventID))
            return false;

        if (!recvPacket->Get(sourceNetworkID))
            return false;

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& networkState = registry->ctx().get<Singletons::NetworkState>();

        if (!networkState.networkIDToEntity.contains(sourceNetworkID))
        {
            NC_LOG_WARNING("Network : Received Combat Event for non existing entity ({0})", entt::to_integral(sourceNetworkID));
            return true;
        }

        entt::entity sourceEntity = networkState.networkIDToEntity[sourceNetworkID];

        if (!registry->valid(sourceEntity))
        {
            NC_LOG_WARNING("Network : Received Combat Event for non existing entity ({0})", entt::to_integral(sourceNetworkID));
            return true;
        }

        switch (eventID)
        {
            // Damage Taken
            case 0:
            case 1:
            {
                entt::entity targetNetworkID = entt::null;
                f32 value = 0.0f;

                if (!recvPacket->Get(targetNetworkID))
                    return false;

                if (!recvPacket->GetF32(value))
                    return false;

                if (!networkState.networkIDToEntity.contains(targetNetworkID))
                {
                    NC_LOG_WARNING("Network : Received Combat Event for non existing target entity ({0})", entt::to_integral(targetNetworkID));
                    return true;
                }

                entt::entity targetEntity = networkState.networkIDToEntity[targetNetworkID];

                if (!registry->valid(targetEntity))
                {
                    NC_LOG_WARNING("Network : Received Combat Event for non existing target entity ({0})", entt::to_integral(targetNetworkID));
                    return true;
                }

                Components::Name* sourceName = registry->try_get<Components::Name>(sourceEntity);
                Components::Name* targetName = registry->try_get<Components::Name>(targetEntity);

                if (!sourceName || !targetName)
                {
                    NC_LOG_WARNING("Network : Received Combat Event for entity without Name Component ({0})", entt::to_integral(targetNetworkID));
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
                entt::entity targetNetworkID = entt::null;

                if (!recvPacket->Get(targetNetworkID))
                    return false;

                if (!networkState.networkIDToEntity.contains(targetNetworkID))
                {
                    NC_LOG_WARNING("Network : Received Combat Event for non existing target entity ({0})", entt::to_integral(targetNetworkID));
                    return true;
                }

                entt::entity targetEntity = networkState.networkIDToEntity[targetNetworkID];

                if (!registry->valid(targetEntity))
                {
                    NC_LOG_WARNING("Network : Received Combat Event for non existing target entity ({0})", entt::to_integral(targetNetworkID));
                    return true;
                }

                Components::Name* sourceName = registry->try_get<Components::Name>(sourceEntity);
                Components::Name* targetName = registry->try_get<Components::Name>(targetEntity);

                if (!sourceName || !targetName)
                {
                    NC_LOG_WARNING("Network : Received Combat Event for entity without Name Component ({0})", entt::to_integral(targetNetworkID));
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

    void NetworkConnection::Init(entt::registry& registry)
    {
        entt::registry::context& ctx = registry.ctx();

        auto& networkState = ctx.emplace<Singletons::NetworkState>();

        // Setup NetworkState
        {
            networkState.client = std::make_unique<Network::Client>();
            networkState.networkIDToEntity.reserve(1024);
            networkState.entityToNetworkID.reserve(1024);

            networkState.gameMessageRouter = std::make_unique<Network::GameMessageRouter>();
            networkState.gameMessageRouter->SetMessageHandler(Network::GameOpcode::Server_Connected,                Network::GameMessageHandler(Network::ConnectionStatus::None,        0u, -1, &HandleOnConnected));
            
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

            networkState.gameMessageRouter->SetMessageHandler(Network::GameOpcode::Server_SendSpellCastResult,      Network::GameMessageHandler(Network::ConnectionStatus::Connected,   0u, -1, &HandleOnSpellCastResult));

            networkState.gameMessageRouter->SetMessageHandler(Network::GameOpcode::Server_SendCombatEvent,          Network::GameMessageHandler(Network::ConnectionStatus::Connected,   0u, -1, &HandleOnCombatEvent));
        }
    }

    void NetworkConnection::Update(entt::registry& registry, f32 deltaTime)
    {
        entt::registry::context& ctx = registry.ctx();

        auto& networkState = ctx.get<Singletons::NetworkState>();
        
        static bool wasConnected = false;
        if (networkState.client->IsConnected())
        {
            if (!wasConnected)
            {
                // Just connected
                wasConnected = true;

                CharacterController::DeleteCharacterController(registry);
            }
        }
        else
        {
            if (wasConnected)
            {
                // Just Disconnected
                wasConnected = false;

                NC_LOG_WARNING("Network : Disconnected");

                CharacterController::DeleteCharacterController(registry);
                CharacterController::InitCharacterController(registry, true);
            }

            return;
        }

        Network::Socket::Result readResult = networkState.client->Read();
        if (readResult == Network::Socket::Result::SUCCESS)
        {
            std::shared_ptr<Bytebuffer>& buffer = networkState.client->GetReadBuffer();
            while (size_t activeSize = buffer->GetActiveSize())
            {
                // We have received a partial header and need to read more
                if (activeSize < sizeof(Network::PacketHeader))
                {
                    buffer->Normalize();
                    break;
                }

                Network::PacketHeader* header = reinterpret_cast<Network::PacketHeader*>(buffer->GetReadPointer());

                if (header->size > Network::DEFAULT_BUFFER_SIZE)
                {
#ifdef NC_DEBUG
                    NC_LOG_ERROR("Network : Received Invalid Opcode Size ({0} : {1}) from server", static_cast<Network::OpcodeType>(header->opcode), header->size);
#endif // NC_DEBUG
                    networkState.client->Close();
                    break;
                }

                size_t receivedPayloadSize = activeSize - sizeof(Network::PacketHeader);
                if (receivedPayloadSize < header->size)
                {
                    buffer->Normalize();
                    break;
                }

                buffer->SkipRead(sizeof(Network::PacketHeader));

                std::shared_ptr<Bytebuffer> messageBuffer = Bytebuffer::Borrow<Network::DEFAULT_BUFFER_SIZE>();
                {
                    // Payload
                    {
                        messageBuffer->Put(*header);

                        if (header->size)
                        {
                            std::memcpy(messageBuffer->GetWritePointer(), buffer->GetReadPointer(), header->size);
                            messageBuffer->SkipWrite(header->size);

                            // Skip Payload
                            buffer->SkipRead(header->size);
                        }
                    }

                    if (!networkState.gameMessageRouter->CallHandler(Network::SOCKET_ID_INVALID, messageBuffer))
                    {
                        networkState.client->Close();
                    }
                }
            }
            
            if (buffer->GetActiveSize() == 0)
            {
                buffer->Reset();
            }
        }
    }
}