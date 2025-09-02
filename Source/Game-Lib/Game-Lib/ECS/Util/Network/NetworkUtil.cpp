#include "NetworkUtil.h"

#include <entt/entt.hpp>

namespace ECS::Util::Network
{
    bool IsConnected(::ECS::Singletons::NetworkState& networkState)
    {
        bool isConnected = networkState.client && networkState.client->IsConnected();
        return isConnected;
    }

    bool IsEntityKnown(::ECS::Singletons::NetworkState& networkState, entt::entity entity)
    {
        bool isKnown = networkState.entityToNetworkID.contains(entity);
        return isKnown;
    }

    bool IsObjectGUIDKnown(::ECS::Singletons::NetworkState& networkState, ObjectGUID guid)
    {
        bool isKnown = networkState.networkIDToEntity.contains(guid);
        return isKnown;
    }

    bool GetObjectGUIDFromEntityID(::ECS::Singletons::NetworkState& networkState, entt::entity entity, ObjectGUID& guid)
    {
        guid = ObjectGUID::Empty;

        if (!IsEntityKnown(networkState, entity))
            return false;

        guid = networkState.entityToNetworkID[entity];
        return true;
    }

    bool GetEntityIDFromObjectGUID(::ECS::Singletons::NetworkState& networkState, ObjectGUID guid, entt::entity& entity)
    {
        entity = entt::null;
        if (!IsObjectGUIDKnown(networkState, guid))
            return false;

        entity = networkState.networkIDToEntity[guid];
        return true;
    }

    bool SendPacket(Singletons::NetworkState& networkState, std::shared_ptr<Bytebuffer>& buffer)
    {
        if (!IsConnected(networkState))
            return false;

        networkState.client->Send(buffer);
        return true;
    }
}