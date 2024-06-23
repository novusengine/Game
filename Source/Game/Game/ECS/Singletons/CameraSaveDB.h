#pragma once
#include "ClientDBCollection.h"
#include "Game/Util/ServiceLocator.h"

#include <Base/Types.h>

#include <FileFormat/Novus/ClientDB/ClientDB.h>
#include <FileFormat/Novus/ClientDB/Definitions.h>

#include <entt/entt.hpp>
#include <robinhood/robinhood.h>

namespace ECS::Singletons
{
    struct CameraSaveDB
    {
    public:
        CameraSaveDB() { }

        bool Refresh()
        {
            entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
            entt::registry::context& ctx = registry->ctx();

            ClientDBCollection& clientDBCollection = ctx.get<ClientDBCollection>();
            auto* cameraSaves = clientDBCollection.Get<ClientDB::Definitions::CameraSave>(ClientDBHash::CameraSave);

            cameraSaveNameHashToID.clear();

            u32 numRecords = cameraSaves->Count();
            cameraSaveNameHashToID.reserve(numRecords);

            cameraSaves->Each([this](auto* storage, const ClientDB::Definitions::CameraSave* cameraSave)
            {
                u32 nameHash = StringUtils::fnv1a_32(cameraSave->name.c_str(), cameraSave->name.length());

                cameraSaveNameHashToID[nameHash] = cameraSave->id;
            });

            return true;
        }

    public:
        robin_hood::unordered_map<u32, u32> cameraSaveNameHashToID;
    };
}