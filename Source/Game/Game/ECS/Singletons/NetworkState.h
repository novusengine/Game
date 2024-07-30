#pragma once
#include <robinhood/robinhood.h>

#include <memory>

namespace Network
{
    class Client;
    class GameMessageRouter;
}

namespace ECS::Singletons
{
    struct NetworkState
    {
    public:
        std::unique_ptr<Network::Client> client;
        std::unique_ptr<Network::GameMessageRouter> gameMessageRouter;

        robin_hood::unordered_map<entt::entity, entt::entity> networkIDToEntity;
        robin_hood::unordered_map<entt::entity, entt::entity> entityToNetworkID;
    };
}