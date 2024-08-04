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
        static constexpr u64 PING_INTERVAL = 5000;

        std::unique_ptr<Network::Client> client;
        std::unique_ptr<Network::GameMessageRouter> gameMessageRouter;

        u64 lastPingTime = 0;
        u64 lastPongTime = 0;

        u8 pingHistoryIndex = 0;
        u16 ping = 0;
        u8 serverNetworkDiff = 0;
        u8 serverUpdateDiff = 0;
        std::array<u16, 6> pingHistory = { 0, 0, 0, 0, 0, 0 };
        u8 pingHistorySize = 0;

        robin_hood::unordered_map<entt::entity, entt::entity> networkIDToEntity;
        robin_hood::unordered_map<entt::entity, entt::entity> entityToNetworkID;
    };
}