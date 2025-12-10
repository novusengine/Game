#pragma once
#include <Gameplay/GameDefine.h>
#include <Gameplay/Network/Define.h>

#include <robinhood/robinhood.h>

#include <asio/asio.hpp>
#include <entt/entt.hpp>
#include <libsodium/sodium.h>
#include <RTree/RTree.h>
#include <spake2-ee/crypto_spake.h>

#include <memory>

namespace Network
{
    class Client;
    class GameMessageRouter;
}

namespace ECS
{
    enum class AuthenticationStage : u8
    {
        None = 0,
        Step1 = 1,
        Step2 = 2,
        Failed = 3,
        Completed = 4
    };

    struct CharacterListEntry
    {
    public:
        std::string name;
        u8 race;
        u8 gender;
        u8 unitClass;
        u16 level;
        u16 mapID;
    };

    struct AuthenticationInfo
    {
    public:
        void Reset()
        {
            stage = AuthenticationStage::None;
            username.clear();
            password.clear();
            sodium_memzero(&state, sizeof(state));
            sodium_memzero(&sharedKeys, sizeof(sharedKeys));
        }

    public:
        AuthenticationStage stage = AuthenticationStage::None;
        std::string username = "";
        std::string password = "";

        crypto_spake_client_state state;
        crypto_spake_shared_keys_ sharedKeys;
    };

    struct CharacterListInfo
    {
    public:
        void Reset()
        {
            characterSelected = false;
            list.clear();
            nameHashToIndex.clear();
            nameHashToSortingIndex.clear();
        }

    public:
        bool characterSelected = false;
        std::vector<CharacterListEntry> list;
        robin_hood::unordered_map<u32, u32> nameHashToIndex;
        robin_hood::unordered_map<u32, u32> nameHashToSortingIndex;
    };

    struct PingInfo
    {
    public:
        void Reset()
        {
            lastPingTime = 0u;
            lastPongTime = 0u;
            ping = 0;
            serverUpdateDiff = 0;
            pingHistoryIndex = 0;
            pingHistory.fill(0);
            pingHistorySize = 0;
        }

    public:
        u64 lastPingTime = 0;
        u64 lastPongTime = 0;

        u8 pingHistoryIndex = 0;
        u16 ping = 0;
        u8 serverUpdateDiff = 0;
        std::array<u16, 6> pingHistory = { 0, 0, 0, 0, 0, 0 };
        u8 pingHistorySize = 0;
    };

    namespace Singletons
    {
        struct NetworkState
        {
        public:
            static constexpr u64 PING_INTERVAL = 5000;

            std::thread asioThread;
            asio::io_context asioContext;
            std::shared_ptr<asio::ip::tcp::resolver> resolver;
            std::unique_ptr<Network::Client> client;
            std::unique_ptr<Network::GameMessageRouter> gameMessageRouter;

            bool isLoadingMap = false;
            bool isInWorld = false;

            AuthenticationInfo authInfo;
            CharacterListInfo characterListInfo;
            PingInfo pingInfo;

            Network::ObjectNetFieldsListener objectNetFieldListener;
            Network::UnitNetFieldsListener unitNetFieldListener;

            std::vector<vec3> pathToVisualize;

            robin_hood::unordered_map<ObjectGUID, entt::entity> networkIDToEntity;
            robin_hood::unordered_map<entt::entity, ObjectGUID> entityToNetworkID;
            RTree<ObjectGUID, f32, 3>* networkVisTree = nullptr;
        };
    }
}