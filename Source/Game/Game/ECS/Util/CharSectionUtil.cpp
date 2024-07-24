#include "CharSectionUtil.h"
#include "Game/ECS/Singletons/CharSectionSingleton.h"
#include "Game/ECS/Singletons/ClientDBCollection.h"

#include <FileFormat/Novus/ClientDB/Definitions.h>

#include <entt/entt.hpp>

namespace ECS::Util
{
    namespace CharSection
    {
        void RefreshData(entt::registry& registry)
        {
            entt::registry::context& ctx = registry.ctx();

            if (!ctx.find<ECS::Singletons::CharSectionSingleton>())
            {
                ctx.emplace<ECS::Singletons::CharSectionSingleton>();
            }

            auto& charSectionSingleton = ctx.get<ECS::Singletons::CharSectionSingleton>();
            charSectionSingleton.keyToCharSectionID.clear();

            auto& clientDBCollection = ctx.get<ECS::Singletons::ClientDBCollection>();
            auto* charSectionStorage = clientDBCollection.Get<ClientDB::Definitions::CharSection>(Singletons::ClientDBHash::CharSection);

            if (!charSectionStorage)
                return;

            charSectionStorage->Each([&](const auto* storage, const ClientDB::Definitions::CharSection* charSection)
            {
                u32 key = charSection->raceID;
                key |= charSection->sexID << 4;
                key |= charSection->baseSection << 5;
                key |= charSection->varationIndex << 9;
                key |= charSection->colorIndex << 14;
                key |= charSection->flags << 19;

                charSectionSingleton.keyToCharSectionID[key] = charSection->id;
            });
        }
    }
}