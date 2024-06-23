#include "NetworkConnection.h"

#include "Game/ECS/Singletons/NetworkState.h"
#include "Game/Util/ServiceLocator.h"

#include <Base/CVarSystem/CVarSystem.h>
#include <Base/Util/DebugHandler.h>

#include <Network/Client.h>
#include <Network/Define.h>
#include <Network/Packet.h>
#include <Network/PacketHandler.h>

#include <entt/entt.hpp>

AutoCVar_Int CVAR_DebugAutoConnectToGameserver(CVarCategory::Client | CVarCategory::Network, "autoConnectToGameserver", "Automatically connect to game server when launching client", 0, CVarFlags::EditCheckbox);

namespace ECS::Systems
{
    bool HandleOnConnected(Network::SocketID socketID, std::shared_ptr<Network::Packet> netPacket)
    {
        u8 result = 0;
        if (!netPacket->payload->GetU8(result))
            return false;

        if (result == 0)
        {
            std::string charName = "";
            if (!netPacket->payload->GetString(charName))
                return false;

            NC_LOG_INFO("Network : Connected to server (Playing on character : \"{0}\")", charName);
        }

        return true;
    }

    void NetworkConnection::Init(entt::registry& registry)
    {
        entt::registry::context& ctx = registry.ctx();

        Singletons::NetworkState& networkState = ctx.emplace<Singletons::NetworkState>();

        // Setup NetworkState
        {
            networkState.client = std::make_unique<Network::Client>();

            networkState.packetHandler = std::make_unique<Network::PacketHandler>();
            networkState.packetHandler->SetMessageHandler(Network::Opcode::SMSG_CONNECTED, Network::OpcodeHandler(Network::ConnectionStatus::AUTH_NONE, 4u, 12u, &HandleOnConnected));

            Network::Socket::Result initResult = networkState.client->Init(Network::Socket::Mode::TCP);
            if (initResult != Network::Socket::Result::SUCCESS)
            {
                NC_LOG_ERROR("Network : Failed to initialize Client");
                return;
            }

            networkState.client->GetSocket()->SetBlockingState(false);

            if (CVAR_DebugAutoConnectToGameserver.Get() == 1)
            {
                // Connect to IP/Port
                std::string ipAddress = "127.0.0.1";
                u16 port = 4000;

                Network::Socket::Result connectResult = networkState.client->Connect(ipAddress, port);
                if (connectResult != Network::Socket::Result::SUCCESS &&
                    connectResult != Network::Socket::Result::ERROR_WOULD_BLOCK)
                {
                    NC_LOG_ERROR("Network : Failed to connect to ({0}, {1})", ipAddress, port);
                }
            }
        }
    }

    void NetworkConnection::Update(entt::registry& registry, f32 deltaTime)
    {
        entt::registry::context& ctx = registry.ctx();

        Singletons::NetworkState& networkState = ctx.get<Singletons::NetworkState>();
        
        static bool wasConnected = false;
        if (networkState.client->IsConnected())
        {
            if (!wasConnected)
            {
                // Just connected
                wasConnected = true;
            }
        }
        else
        {
            if (wasConnected)
            {
                // Just Disconnected
                wasConnected = false;

                NC_LOG_WARNING("Network : Disconnected");
            }
        }

        Network::Socket::Result readResult = networkState.client->Read();
        if (readResult == Network::Socket::Result::SUCCESS)
        {
            std::shared_ptr<Bytebuffer>& buffer = networkState.client->GetReadBuffer();
            while (size_t activeSize = buffer->GetActiveSize())
            {
                // We have received a partial header and need to read more
                if (activeSize < sizeof(Network::Packet::Header))
                {
                    buffer->Normalize();
                    break;
                }

                Network::Packet::Header* header = reinterpret_cast<Network::Packet::Header*>(buffer->GetReadPointer());

                if (header->opcode == Network::Opcode::INVALID || header->opcode > Network::Opcode::MAX_COUNT)
                {
#ifdef NC_Debug
                    NC_LOG_ERROR("Network : Received Invalid Opcode ({0}) from server", static_cast<std::underlying_type<Network::Opcode>::type>(header->opcode));
#endif // NC_Debug
                    networkState.client->Close();
                    break;
                }

                if (header->size > Network::DEFAULT_BUFFER_SIZE)
                {
#ifdef NC_Debug
                    NC_LOG_ERROR("Network : Received Invalid Opcode Size ({0} : {1}) from server", static_cast<std::underlying_type<Network::Opcode>::type>(header->opcode), header->size);
#endif // NC_Debug
                    networkState.client->Close();
                    break;
                }

                size_t receivedPayloadSize = activeSize - sizeof(Network::Packet::Header);
                if (receivedPayloadSize < header->size)
                {
                    buffer->Normalize();
                    break;
                }

                buffer->SkipRead(sizeof(Network::Packet::Header));

                std::shared_ptr<Network::Packet> packet = Network::Packet::Borrow();
                {
                    // Header
                    {
                        packet->header = *header;
                    }

                    // Payload
                    {
                        if (packet->header.size)
                        {
                            packet->payload = Bytebuffer::Borrow<Network::DEFAULT_BUFFER_SIZE>();
                            packet->payload->size = packet->header.size;
                            packet->payload->writtenData = packet->header.size;
                            std::memcpy(packet->payload->GetDataPointer(), buffer->GetReadPointer(), packet->header.size);

                            // Skip Payload
                            buffer->SkipRead(header->size);
                        }
                    }

                    if (!networkState.packetHandler->CallHandler(Network::SOCKET_ID_INVALID, packet))
                    {
                        networkState.client->Close();
                    }
                }
            }
        }
    }
}