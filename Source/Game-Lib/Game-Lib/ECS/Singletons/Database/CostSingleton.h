#pragma once

#include <Base/Types.h>

#include <robinhood/robinhood.h>

#include <string>
#include <vector>

namespace ECS
{
    namespace Singletons
    {
        struct CostSingleton
        {
        public:
            struct CurrencyComponent
            {
            public:
                u16 currencyID = 0;
                u64 amount = 0;
            };

            struct ItemComponent
            {
            public:
                u32 itemTemplateID = 0;
                u32 amount = 0;
            };

        public:
            robin_hood::unordered_flat_map<u32, std::vector<CurrencyComponent>> currencyComponentsByCostID;
            robin_hood::unordered_flat_map<u32, std::vector<ItemComponent>> itemComponentsByCostID;
            robin_hood::unordered_flat_map<u16, std::string> currencyNamesByID;
        };
    }
}
