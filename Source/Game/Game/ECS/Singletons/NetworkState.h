#pragma once
#include <robinhood/robinhood.h>

#include <memory>

namespace Network
{
    class Client;
    class PacketHandler;
}

namespace ECS::Singletons
{
    struct NetworkState
    {
    public:
        std::string characterName = "dev";

        std::unique_ptr<Network::Client> client;
        std::unique_ptr<Network::PacketHandler> packetHandler;

        robin_hood::unordered_map<entt::entity, entt::entity> networkIDToEntity;
        robin_hood::unordered_map<entt::entity, entt::entity> entityToNetworkID;
    };
}