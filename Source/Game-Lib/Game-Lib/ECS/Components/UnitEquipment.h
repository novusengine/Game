#pragma once
#include "Game-Lib/Gameplay/Database/Item.h"

#include <Base/Types.h>

#include <Meta/Generated/Shared/UnitEnum.h>

#include <entt/fwd.hpp>

#include <robinhood/robinhood.h>

namespace ECS
{
    namespace Components
    {
        struct UnitEquipment
        {
        public:
            std::array<u32, (u32)Generated::ItemEquipSlotEnum::EquipmentEnd + 1u> equipmentSlotToItemID;
            robin_hood::unordered_set<Generated::ItemEquipSlotEnum> dirtyItemIDSlots;

            std::array<u32, (u32)Generated::ItemEquipSlotEnum::EquipmentEnd + 1u> equipmentSlotToVisualItemID;
            robin_hood::unordered_set<Generated::ItemEquipSlotEnum> dirtyVisualItemIDSlots;
        };

        struct UnitEquipmentDirty { };
        struct UnitVisualEquipmentDirty { };
    }
}