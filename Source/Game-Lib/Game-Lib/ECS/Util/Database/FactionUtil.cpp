#include "FactionUtil.h"

#include "Game-Lib/Application/EnttRegistries.h"
#include "Game-Lib/ECS/Singletons/Database/ClientDBSingleton.h"
#include "Game-Lib/ECS/Singletons/Database/FactionSingleton.h"
#include "Game-Lib/ECS/Util/FactionUtil.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Base/Util/DebugHandler.h>

#include <FileFormat/Novus/ClientDB/ClientDB.h>

#include <MetaGen/Shared/ClientDB/ClientDB.h>

#include <entt/entt.hpp>

#include <limits>

namespace ECSUtil::Faction
{
    bool Refresh()
    {
        EnttRegistries* registries = ServiceLocator::GetEnttRegistries();
        entt::registry& dbRegistry = *registries->dbRegistry;
        auto& clientDBSingleton = dbRegistry.ctx().get<ECS::Singletons::ClientDBSingleton>();

        Gameplay::Faction::FactionContent content;
        const bool hasFaction = clientDBSingleton.Has(ClientDBHash::Faction);
        const bool hasRelations = clientDBSingleton.Has(ClientDBHash::FactionRelation);
        const bool hasStandings = clientDBSingleton.Has(ClientDBHash::FactionStanding);
        bool validContent = hasFaction && hasRelations && hasStandings;

        if (!validContent)
            NC_LOG_ERROR("Faction ClientDB content is incomplete; Faction, FactionRelation, and FactionStanding are all required");

        ClientDB::Data* factionStorage = hasFaction ? clientDBSingleton.Get(ClientDBHash::Faction) : nullptr;
        ClientDB::Data* relationStorage = hasRelations ? clientDBSingleton.Get(ClientDBHash::FactionRelation) : nullptr;
        ClientDB::Data* standingStorage = hasStandings ? clientDBSingleton.Get(ClientDBHash::FactionStanding) : nullptr;

        if (validContent)
        {
            content.definitions.reserve(factionStorage->GetNumRows());
            factionStorage->Each([&](u32 id, MetaGen::Shared::ClientDB::FactionRecord& record)
            {
                if (id > std::numeric_limits<Gameplay::Faction::FactionID>::max())
                {
                    NC_LOG_ERROR("Skipped client faction row {0}: stable faction IDs are U16", id);
                    return true;
                }

                content.definitions.push_back({
                    .id = static_cast<Gameplay::Faction::FactionID>(id),
                    .name = factionStorage->GetString(record.name),
                    .flags = record.flags,
                    .defaultReactionToOthers = record.defaultReactionToOthers,
                    .defaultPlayerReactionMin = record.defaultPlayerReactionMin,
                    .defaultPlayerReactionMax = record.defaultPlayerReactionMax,
                    .defaultReputationValue = record.defaultReputationValue
                });

                return true;
            });

            content.relations.reserve(relationStorage->GetNumRows());
            relationStorage->Each([&](u32 id, MetaGen::Shared::ClientDB::FactionRelationRecord& record)
            {
                if (id == 0)
                    return true;

                content.relations.push_back({ .sourceFactionID = record.sourceFactionID, .targetFactionID = record.targetFactionID, .reaction = record.reaction });
                return true;
            });

            content.standings.reserve(standingStorage->GetNumRows());
            standingStorage->Each([&](u32 id, MetaGen::Shared::ClientDB::FactionStandingRecord& record)
            {
                const std::string& name = standingStorage->GetString(record.name);
                if (id == 0 && name.empty() && record.minimumValue == 0 && record.reaction == 0 && record.sortOrder == 0)
                    return true;

                if (id > std::numeric_limits<Gameplay::Faction::StandingID>::max())
                {
                    NC_LOG_ERROR("Skipped client faction standing row {0}: stable standing IDs are U16", id);
                    return true;
                }

                content.standings.push_back({
                    .id = static_cast<Gameplay::Faction::StandingID>(id),
                    .name = name,
                    .minimumValue = record.minimumValue,
                    .reaction = record.reaction,
                    .sortOrder = record.sortOrder
                });

                return true;
            });
        }

        std::shared_ptr<const Gameplay::Faction::FactionRuntimeData> runtime = Gameplay::Faction::BuildRuntimeData(content);
        auto& factionSingleton = dbRegistry.ctx().contains<ECS::Singletons::FactionSingleton>()
            ? dbRegistry.ctx().get<ECS::Singletons::FactionSingleton>()
            : dbRegistry.ctx().emplace<ECS::Singletons::FactionSingleton>();

        factionSingleton.runtime = runtime;
        ECS::Util::Faction::Initialize(*registries->gameRegistry, std::move(runtime));

        NC_LOG_INFO("Compiled {0} client factions, {1} directional relations, and {2} standing thresholds", content.definitions.size(), content.relations.size(), content.standings.size());
        return validContent;
    }
} // namespace ECSUtil::Faction
