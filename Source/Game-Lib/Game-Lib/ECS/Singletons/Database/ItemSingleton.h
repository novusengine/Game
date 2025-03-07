#pragma once
#include <Base/Types.h>

#include <Gameplay/GameDefine.h>

#include <robinhood/robinhood.h>
#include <entt/entt.hpp>

#include <array>

namespace ECS
{
    namespace Singletons
    {
        struct ItemSingleton
        {
        public:
            struct ItemToEffectMapping
            {
            public:
                u32 indexIntoMap;
                u32 count;
            };

            struct HelmModelMapping
            {
            public:
                static constexpr u32 NumArraySlots = ((u32)GameDefine::UnitRace::Count * 2);

                // This maps as follows (RaceID * 2 == Base Index, Male == +0, Female == +1)
                std::array<u32, NumArraySlots> raceGenderToModelHash = { };
            };

            struct ShoulderModelMapping
            {
            public:
                static constexpr u32 NumArraySlots = 2;

                // This maps as follows (Left == 0, Right == 1)
                std::array<u32, NumArraySlots> sideToModelHash = { };
            };

            struct ItemDisplayInfoComponentSectionData
            {
            public:
                robin_hood::unordered_map<u8, u32> componentSectionToTextureHash;
            };

        public:
            ItemSingleton() {}

            robin_hood::unordered_set<u32> itemIDs;
            robin_hood::unordered_map<u32, u32> itemIDToStatTemplateID;
            robin_hood::unordered_map<u32, u32> itemIDToArmorTemplateID;
            robin_hood::unordered_map<u32, u32> itemIDToWeaponTemplateID;
            robin_hood::unordered_map<u32, u32> itemIDToShieldTemplateID;

            robin_hood::unordered_map<u32, ItemToEffectMapping> itemIDToEffectMapping;
            std::vector<u32> itemEffectIDs;

            robin_hood::unordered_map<u32, HelmModelMapping> helmModelResourcesIDToModelMapping;
            robin_hood::unordered_map<u32, ShoulderModelMapping> shoulderModelResourcesIDToModelMapping;

            robin_hood::unordered_map<u64, u32> itemDisplayInfoMaterialResourcesKeyToID;
            robin_hood::unordered_map<u32, ItemDisplayInfoComponentSectionData> itemDisplayInfoToComponentSectionData;
        };
    }
}