#pragma once
#include <Base/Types.h>

#include <entt/fwd.hpp>

#include <robinhood/robinhood.h>

namespace ECS
{
    struct AuraInfo
    {
    public:
        u32 unitID;
        u32 auraID;

        u32 spellID;
        u64 expireTimestamp;
        u16 stacks;
    };

    namespace Components
    {
        struct UnitAuraInfo
        {
        public:
            std::vector<AuraInfo> auras;
            robin_hood::unordered_map<u32, u32> auraIDToAuraIndex;
            robin_hood::unordered_map<u32, u32> spellIDToAuraIndex;
        };
    }
}