#include "LightUtil.h"

#include "Game-Lib/ECS/Singletons/AreaLightInfo.h"
#include "Game-Lib/ECS/Singletons/Database/ClientDBSingleton.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <MetaGen/Shared/ClientDB/ClientDB.h>

#include <entt/entt.hpp>

namespace ECSUtil::Light
{
    bool Refresh()
    {
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->dbRegistry;
        auto& ctx = registry->ctx();

        if (!ctx.find<ECS::Singletons::AreaLightInfo>())
            ctx.emplace<ECS::Singletons::AreaLightInfo>();

        auto& clientDBSingleton = ctx.get<ECS::Singletons::ClientDBSingleton>();
        bool hasLightCDBs = clientDBSingleton.Has(ClientDBHash::Light) && clientDBSingleton.Has(ClientDBHash::LightData) && clientDBSingleton.Has(ClientDBHash::LightParams);
        if (!hasLightCDBs)
            return false;

        auto* lightStorage = clientDBSingleton.Get(ClientDBHash::Light);
        auto* lightParamsStorage = clientDBSingleton.Get(ClientDBHash::LightParams);
        auto* lightDataStorage = clientDBSingleton.Get(ClientDBHash::LightData);

        auto& areaLightInfo = ctx.get<ECS::Singletons::AreaLightInfo>();
        areaLightInfo.finalColorData = { };
        areaLightInfo.activeAreaLights.clear();
        areaLightInfo.activeAreaLights.reserve(8);

        // Lights
        {
            u32 numMaps = lightStorage->GetNumRows();
            areaLightInfo.mapIDToLightIDs.clear();
            areaLightInfo.mapIDToLightIDs.reserve(numMaps);

            lightStorage->Each([&](u32 id, const MetaGen::Shared::ClientDB::LightRecord& light) -> bool
            {
                u16 mapID = light.mapID;

                auto& lightIDs = areaLightInfo.mapIDToLightIDs[mapID];

                if (lightIDs.size() == 0)
                    lightIDs.reserve(16);

                lightIDs.push_back(id);
                return true;
            });
        }

        // Light Params
        {
            u32 numLightParams = lightParamsStorage->GetNumRows();

            areaLightInfo.lightParamIDToLightData.clear();
            areaLightInfo.lightParamIDToLightData.reserve(numLightParams);

            lightDataStorage->Each([&](u32 id, const MetaGen::Shared::ClientDB::LightDataRecord& lightData) -> bool
            {
                u16 lightParamID = lightData.lightParamID;

                auto& lightDataIDs = areaLightInfo.lightParamIDToLightData[lightParamID];

                if (lightDataIDs.size() == 0)
                    lightDataIDs.reserve(16);

                lightDataIDs.push_back(id);
                return true;
            });
        }

        return true;
    }
}
