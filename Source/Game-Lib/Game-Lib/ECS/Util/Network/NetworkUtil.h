#pragma once
#include "Game-Lib/ECS/Singletons/NetworkState.h"

#include <Base/Types.h>
#include <Base/Memory/Bytebuffer.h>

#include <Network/Define.h>
#include <Network/Client.h>

#include <entt/fwd.hpp>

namespace ECS
{
    namespace Util::Network
    {
        bool IsConnected(::ECS::Singletons::NetworkState& networkState);
        bool IsEntityKnown(::ECS::Singletons::NetworkState& networkState, entt::entity entity);
        bool IsObjectGUIDKnown(::ECS::Singletons::NetworkState& networkState, ObjectGUID guid);

        bool GetObjectGUIDFromEntityID(::ECS::Singletons::NetworkState& networkState, entt::entity entity, ObjectGUID& guid);
        bool GetEntityIDFromObjectGUID(::ECS::Singletons::NetworkState& networkState, ObjectGUID guid, entt::entity& entity);

        bool SendPacket(Singletons::NetworkState& networkState, std::shared_ptr<Bytebuffer>& buffer);

        template <::Network::PacketConcept... Packets>
        std::shared_ptr<Bytebuffer> CreatePacketBuffer(Packets&&... packets)
        {
            u32 totalSize = ((sizeof(::Network::MessageHeader) + packets.GetSerializedSize()) + ...);
            std::shared_ptr<Bytebuffer> buffer = Bytebuffer::BorrowRuntime(totalSize);

            auto AppendPacket = [&](auto&& packet)
            {
                using PacketType = std::decay_t<decltype(packet)>;

                ::Network::MessageHeader header =
                {
                    .opcode = PacketType::PACKET_ID,
                    .size = packet.GetSerializedSize()
                };

                buffer->Put(header);
                buffer->Serialize(packet);
            };

            (AppendPacket(std::forward<Packets>(packets)), ...);
            return buffer;
        }

        template <::Network::PacketConcept... Packets>
        bool AppendPacketToBuffer(std::shared_ptr<Bytebuffer>& buffer, Packets&&... packets)
        {
            bool failed = false;
            u32 totalSize = ((sizeof(::Network::MessageHeader) + std::decay_t<Packets>().GetSerializedSize()) + ...);

            auto AppendPacket = [&](auto&& packet)
            {
                using PacketType = std::decay_t<decltype(packet)>;

                ::Network::MessageHeader header =
                {
                    .opcode = PacketType::PACKET_ID,
                    .size = packet.GetSerializedSize()
                };

                failed |= !buffer->Put(header);
                failed |= !buffer->Serialize(packet);
            };

            (AppendPacket(std::forward<Packets>(packets)), ...);
            return !failed;
        }

        template <::Network::PacketConcept... Packets>
        bool SendPacket(Singletons::NetworkState& networkState, Packets&&... packets)
        {
            if (!IsConnected(networkState))
                return false;

            std::shared_ptr<Bytebuffer> buffer = CreatePacketBuffer(std::forward<Packets>(packets)...);
            networkState.client->Send(buffer);
            return true;
        }
    }
}