#include "CostUtil.h"

#include "Game-Lib/ECS/Singletons/Database/ClientDBSingleton.h"
#include "Game-Lib/ECS/Singletons/Database/CostSingleton.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <MetaGen/Shared/ClientDB/ClientDB.h>

#include <entt/entt.hpp>

#include <limits>

namespace ECSUtil::Cost
{
    bool Refresh()
    {
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->dbRegistry;
        auto& ctx = registry->ctx();

        if (!ctx.contains<ECS::Singletons::CostSingleton>())
            ctx.emplace<ECS::Singletons::CostSingleton>();

        auto& costSingleton = ctx.get<ECS::Singletons::CostSingleton>();
        auto& clientDBSingleton = ctx.get<ECS::Singletons::ClientDBSingleton>();

        costSingleton.currencyComponentsByCostID.clear();
        costSingleton.itemComponentsByCostID.clear();
        costSingleton.currencyNamesByID.clear();

        if (clientDBSingleton.Has(ClientDBHash::Currency))
        {
            ::ClientDB::Data* currencies = clientDBSingleton.Get(ClientDBHash::Currency);
            currencies->Each([&](u32 id, const MetaGen::Shared::ClientDB::CurrencyRecord& currency)
            {
                if (id <= std::numeric_limits<u16>::max())
                    costSingleton.currencyNamesByID[static_cast<u16>(id)] = currencies->GetString(currency.name);
                return true;
            });
        }

        if (clientDBSingleton.Has(ClientDBHash::CostCurrencyComponent))
        {
            clientDBSingleton.Get(ClientDBHash::CostCurrencyComponent)->Each([&](u32, const MetaGen::Shared::ClientDB::CostCurrencyComponentRecord& component)
            {
                costSingleton.currencyComponentsByCostID[component.costID].push_back({ component.currencyID, component.amount });
                return true;
            });
        }

        if (clientDBSingleton.Has(ClientDBHash::CostItemComponent))
        {
            clientDBSingleton.Get(ClientDBHash::CostItemComponent)->Each([&](u32, const MetaGen::Shared::ClientDB::CostItemComponentRecord& component)
            {
                costSingleton.itemComponentsByCostID[component.costID].push_back({ component.itemTemplateID, component.amount });
                return true;
            });
        }

        return true;
    }
}
