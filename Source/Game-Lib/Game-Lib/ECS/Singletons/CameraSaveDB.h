#pragma once
#include "ClientDBCollection.h"
#include "Game-Lib/Util/ServiceLocator.h"

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

            auto& clientDBCollection = ctx.get<ClientDBCollection>();
            auto* cameraSaves = clientDBCollection.Get(ClientDBHash::CameraSave);

            cameraSaveNameHashToID.clear();

            u32 numRecords = cameraSaves->GetNumRows();
            cameraSaveNameHashToID.reserve(numRecords);

            cameraSaves->Each([this, &cameraSaves](u32 id, const ClientDB::Definitions::CameraSave& cameraSave) -> bool
            {
                const std::string& cameraSaveName = cameraSaves->GetString(cameraSave.name);
                u32 nameHash = StringUtils::fnv1a_32(cameraSaveName.c_str(), cameraSaveName.length());

                cameraSaveNameHashToID[nameHash] = id;
                return true;
            });

            return true;
        }

    public:
        robin_hood::unordered_map<u32, u32> cameraSaveNameHashToID;
    };
}